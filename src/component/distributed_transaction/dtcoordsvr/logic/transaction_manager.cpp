// Copyright 2022 atframework
// Created by owent, on 2022-02-25

#include "logic/transaction_manager.h"

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <memory/object_allocator.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/rpc_utils.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/dtcoordsvr_config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace {
// 协调者自动 reject 宽限时间的默认值：仅在配置缺失或非法时使用，正常以
// dtcoordsvr_cfg.transaction_expire_grace_duration 为准
constexpr int64_t kTransactionExpireGraceSeconds = 5;
// transaction_max_ttl 缺失或非法时的默认值；同时作为上限硬钳制，防止配置错误导致 now+max_ttl 溢出
constexpr int64_t kDefaultTransactionMaxTtlSeconds = 3600;
constexpr int64_t kHardMaxTransactionTtlSeconds = 3 * 365 * 24 * 3600;

static const atfw::distributed_system::config::dtcoordsvr_cfg& get_dtcoordsvr_cfg() {
  return logic_config::me()->get_server_instance_config<atfw::distributed_system::config::dtcoordsvr_cfg>();
}

static std::chrono::seconds get_transaction_expire_grace() {
  auto grace = protobuf_to_chrono_duration(get_dtcoordsvr_cfg().transaction_expire_grace_duration());
  if (grace < std::chrono::system_clock::duration::zero()) {
    grace = std::chrono::seconds{kTransactionExpireGraceSeconds};
  }
  return std::chrono::ceil<std::chrono::seconds>(grace);
}

static std::chrono::seconds get_transaction_max_ttl() {
  auto max_ttl = protobuf_to_chrono_duration(get_dtcoordsvr_cfg().transaction_max_ttl());
  if (max_ttl <= std::chrono::system_clock::duration::zero()) {
    max_ttl = std::chrono::seconds{kDefaultTransactionMaxTtlSeconds};
  }
  return std::min(std::chrono::ceil<std::chrono::seconds>(max_ttl),
                  std::chrono::seconds{kHardMaxTransactionTtlSeconds});
}

static uint32_t get_transaction_zone_id(const atfw::distributed_system::transaction_metadata& metadata) {
  if (metadata.replicate_read_count() > 0 &&
      static_cast<uint32_t>(metadata.replicate_node_server_id_size()) >= metadata.replicate_read_count()) {
    return logic_config::me()->get_local_zone_id();
  }

  return 0;
}

// TTL 窗口为 expire_timepoint + grace，不允许通过叠加恢复流程的重试等待时间来延长事务生命周期，
// 且不超过 transaction_max_ttl，也不允许永不过期
static uint64_t get_transaction_ttl_seconds(const atfw::distributed_system::transaction_blob_storage& storage) {
  const auto now = atfw::util::time::time_utility::now();
  const auto expire = protobuf_to_system_clock(storage.metadata().expire_timepoint());
  const std::chrono::seconds grace = get_transaction_expire_grace();
  const std::chrono::seconds max_ttl = get_transaction_max_ttl();
  const auto grace_duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(grace);
  const auto max_ttl_duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(max_ttl);

  std::chrono::system_clock::duration ttl = std::chrono::system_clock::duration::zero();
  if (expire <= now) {
    // TTL 使用绝对截止时间 expire + grace；晚到的 save 不能重新获得一整段 grace。
    const auto overdue = now - expire;
    if (overdue < grace_duration) {
      ttl = grace_duration - overdue;
    }
  } else {
    const auto until_expire = expire - now;
    if (until_expire >= max_ttl_duration) {
      ttl = max_ttl_duration;
    } else {
      // 先限制剩余 grace，再做加法，避免 expire + grace 的 time_point 溢出。
      ttl = until_expire + std::min(grace_duration, max_ttl_duration - until_expire);
    }
  }

  return std::max<uint64_t>(1, static_cast<uint64_t>(std::chrono::ceil<std::chrono::seconds>(ttl).count()));
}

static rpc::result_code_type refresh_transaction_ttl(
    rpc::context& ctx, const atfw::distributed_system::transaction_blob_storage& storage) {
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::set_ttl(
      ctx, get_transaction_zone_id(storage.metadata()), storage.metadata().transaction_uuid(),
      get_transaction_ttl_seconds(storage))));
}
}  // namespace

transaction_manager::transaction_manager() : is_exiting_(false), last_stat_timepoint_(0) {}

int transaction_manager::tick() {
  time_t now = atfw::util::time::time_utility::get_now();
  if (last_stat_timepoint_ != now / atfw::util::time::time_utility::MINITE_SECONDS) {
    last_stat_timepoint_ = now / atfw::util::time::time_utility::MINITE_SECONDS;
    FWLOGINFO("[STATISTICS]: current transition cache count: {}", lru_caches_.size());
  }

  int ret = 0;
  if (lru_caches_.empty()) {
    return ret;
  }

  time_t timeout_duration =
      std::chrono::ceil<std::chrono::seconds>(protobuf_to_chrono_duration(get_dtcoordsvr_cfg().lru_expired_duration()))
          .count();
  size_t max_count = get_dtcoordsvr_cfg().lru_max_cache_count();
  for (auto iter = lru_caches_.begin(); iter != lru_caches_.end();) {
    if (!iter->second) {
      iter = lru_caches_.erase(iter);
      ++ret;
      continue;
    }

    const bool is_expired = now > iter->second->last_visit_timepoint + timeout_duration;
    const bool is_over_capacity = lru_caches_.size() > max_count;
    if (!is_over_capacity && !is_expired) {
      // LRU 按访问时间排序，之后的缓存更新，都不会到期
      break;
    }

    // 不跳过有仍在执行的拉取/保存任务的缓存，不随意延长缓存生命周期：
    // 置 removed 标记后淘汰，未完成的 IO 任务结束后缓存立即失效且不会写回或重新入缓存
    if (is_over_capacity && !is_expired && iter->second->data_object.metadata().memory_only()) {
      // memory_only 事务允许容量淘汰（设计如此：允许一定程度不一致，client 端会重新提交状态），但记录日志
      FWLOGWARNING("Evict memory_only transaction {} by capacity, active transaction state will be lost",
                   iter->second->data_key);
    }
    iter->second->removed = true;
    iter = lru_caches_.erase(iter);
    ++ret;
  }

  return ret;
}

rpc::result_code_type transaction_manager::save(rpc::context& ctx, transaction_ptr_type& data) {
  if (!data) {
    // Passing a null handle to the LRU save would dereference it inside await_save; reject early.
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (data->data_object.metadata().memory_only()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  transaction_ptr_type saved_data = data;
  rpc::result_code_type::value_type ret = RPC_AWAIT_CODE_RESULT(lru_caches_.await_save(
      ctx, data,
      [](rpc::context& subctx, const atfw::distributed_system::transaction_blob_storage& in,
         int64_t* out_version) -> rpc::result_code_type {
        uint64_t data_version = 0;
        if (nullptr != out_version) {
          data_version = static_cast<uint64_t>(*out_version);
        }
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> storage{subctx};
        storage->set_zone_id(get_transaction_zone_id(in.metadata()));
        storage->set_transaction_uuid(in.metadata().transaction_uuid());
        if (false == storage->mutable_blob_data()->PackFrom(in)) {
          FWLOGERROR("Serialize transaction_blob_storage failed, {}", storage->blob_data().InitializationErrorString());
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
        }
        int ret = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(subctx, storage, data_version));
        if (nullptr != out_version) {
          *out_version = static_cast<int64_t>(data_version);
        }

        if (ret < 0) {
          RPC_RETURN_CODE(ret);
        }

        // TTL 刷新失败不影响已持久化的数据，创建时已保证设置过 TTL，这里仅记录日志
        rpc::result_code_type::value_type ttl_ret = RPC_AWAIT_CODE_RESULT(refresh_transaction_ttl(subctx, in));
        if (ttl_ret < 0) {
          FWLOGERROR("Refresh transaction {} TTL failed after save, res: {}({})", in.metadata().transaction_uuid(),
                     ttl_ret, protobuf_mini_dumper_get_error_msg(ttl_ret));
        }
        RPC_RETURN_CODE(ret);
      }));

  if (ret != 0 && saved_data) {
    transaction_ptr_type current_data = lru_caches_.get_cache(saved_data->data_key);
    if (current_data == saved_data) {
      lru_caches_.remove_cache(saved_data->data_key);
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::create_transaction(
    rpc::context& ctx, atfw::distributed_system::transaction_blob_storage&& storage) {
  if (storage.metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (storage.participators().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  auto now = atfw::util::time::time_utility::now();
  // 保留 client 的 prepare_timepoint（参与者的 Wound-Wait 锁使用 client 时间口径），仅未设置时才使用协调者本地时间
  if (storage.metadata().prepare_timepoint().seconds() == 0 && storage.metadata().prepare_timepoint().nanos() == 0) {
    protobuf_copy_message(*storage.mutable_metadata()->mutable_prepare_timepoint(), protobuf_from_system_clock(now));
  }

  if (protobuf_to_system_clock(storage.metadata().expire_timepoint()) <= now) {
    const auto& cfg_value = get_dtcoordsvr_cfg().transaction_default_timeout();
    protobuf_copy_message(*storage.mutable_metadata()->mutable_expire_timepoint(),
                          protobuf_from_system_clock(now + protobuf_to_chrono_duration(cfg_value)));
  }
  // 配置错误则fallback 10秒过期
  if (protobuf_to_system_clock(storage.metadata().expire_timepoint()) <= now) {
    protobuf_copy_message(*storage.mutable_metadata()->mutable_expire_timepoint(),
                          protobuf_from_system_clock(now + std::chrono::seconds{10}));
  }

  transaction_lru_map_type::cache_ptr_type transaction_cache_ptr =
      atfw::component::memory::stl::make_strong_rc<transaction_lru_map_type::value_cache_type>(
          storage.metadata().transaction_uuid());
  if (!transaction_cache_ptr) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
  }

  uint64_t db_version = 0;
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> db_data{ctx};
  db_data->set_zone_id(get_transaction_zone_id(storage.metadata()));
  db_data->set_transaction_uuid(storage.metadata().transaction_uuid());
  if (false == db_data->mutable_blob_data()->PackFrom(storage)) {
    FWLOGERROR("Serialize transaction_blob_storage failed, {}", db_data->blob_data().InitializationErrorString());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  if (!storage.metadata().memory_only()) {
    // client 层保证 UUID 不冲突；相同 UUID 的重复 create 来自 client 重试且数据相同，
    // 直接 replace（CAS expected_version=0 为无条件写）即可，重放天然幂等
    ret = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(ctx, db_data, db_version));
    if (ret < 0) {
      FWLOGERROR("rpc::db::distribute_transaction::replace({}) failed, res: {}({})",
                 storage.metadata().transaction_uuid(), ret, protobuf_mini_dumper_get_error_msg(ret));
      RPC_RETURN_CODE(ret);
    }

    ret = RPC_AWAIT_CODE_RESULT(refresh_transaction_ttl(ctx, storage));
    if (ret < 0) {
      FWLOGERROR("Set transaction {} TTL failed after create, res: {}({})", storage.metadata().transaction_uuid(), ret,
                 protobuf_mini_dumper_get_error_msg(ret));
      rpc::result_code_type::value_type remove_result =
          RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::remove_all(
              ctx, get_transaction_zone_id(storage.metadata()), storage.metadata().transaction_uuid()));
      if (remove_result < 0 && remove_result != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
        FWLOGERROR("Remove transaction {} after TTL failure failed, res: {}({})", storage.metadata().transaction_uuid(),
                   remove_result, protobuf_mini_dumper_get_error_msg(remove_result));
      }
      RPC_RETURN_CODE(ret);
    }
  }

  transaction_cache_ptr->data_version = static_cast<int64_t>(db_version);
  protobuf_move_message(transaction_cache_ptr->data_object, std::move(storage));
  lru_caches_.set_cache(transaction_cache_ptr);

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::mutable_transaction(
    rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata, transaction_ptr_type& out) {
  // 停服时返回nullptr
  if (is_exiting_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
  }

  uint32_t zone_id = get_transaction_zone_id(metadata);
  int ret = 0;
  if (!metadata.memory_only()) {
    ret = RPC_AWAIT_CODE_RESULT(lru_caches_.await_fetch(
        ctx, metadata.transaction_uuid(), out,
        [zone_id](rpc::context& subctx, const std::string& key,
                  atfw::distributed_system::transaction_blob_storage& output,
                  int64_t* out_version) -> rpc::result_code_type {
          uint64_t data_version = 0;
          rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> storage{subctx};
          int sub_ret = RPC_AWAIT_CODE_RESULT(
              rpc::db::distribute_transaction::get_all(subctx, zone_id, key, *storage, data_version));
          if (sub_ret < 0) {
            RPC_RETURN_CODE(sub_ret);
          }

          if (false == storage->blob_data().UnpackTo(&output)) {
            std::string error_msg = output.InitializationErrorString();
            if (error_msg.empty() && output.GetDescriptor()->full_name() != storage->blob_data().type_url()) {
              error_msg = "type mismatch, expect: " + std::string(output.GetDescriptor()->full_name()) +
                          " , got: " + std::string(storage->blob_data().type_url());
            }
            FWLOGERROR("ParseFromString transaction_blob_storage failed, {}", error_msg);
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
          }

          if (nullptr != out_version) {
            *out_version = static_cast<int64_t>(data_version);
          }

          RPC_RETURN_CODE(sub_ret);
        }));

    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }
  } else {
    out = lru_caches_.get_cache(metadata.transaction_uuid());
    ret = out ? PROJECT_NAMESPACE_ID::err::EN_SUCCESS : PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  // 读取失败时清空输出句柄：await_fetch 的出错路径可能留下指向未填充缓存对象的强引用，
  // 调用方不应在错误返回时拿到可用的伪记录。
  if (0 != ret) {
    out.reset();
  }

  // 超时且未提交的视为事务失败。
  // 只在读取成功时执行：读取失败（DB 错误、记录损坏、Any 类型不符等）时 out 可能仍指向
  // 未填充的缓存对象，对其做超时判定会把空数据伪造成 REJECTED 并写回 DB，吞掉原始错误。
  if (out &&
      out->data_object.metadata().status() <= atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    auto now = util::time::time_utility::now();
    if (now >
        protobuf_to_system_clock(out->data_object.metadata().expire_timepoint()) + get_transaction_expire_grace()) {
      out->data_object.mutable_metadata()->set_status(
          atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
      protobuf_copy_message(*out->data_object.mutable_metadata()->mutable_finish_timepoint(),
                            protobuf_from_system_clock(now));
      ret = RPC_AWAIT_CODE_RESULT(save(ctx, out));
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_commit(rpc::context& ctx, transaction_ptr_type& trans,
                                                      const std::string& participator_key) {
  if (!trans) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto* all_participators = trans->data_object.mutable_participators();
  if (all_participators == nullptr) {
    FWLOGWARNING("Transaction {} commit for participator {}, has no participators",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  atfw::distributed_system::transaction_participator* selected_participator = nullptr;
  bool has_changed = false;
  for (auto& participator : *all_participators) {
    atfw::distributed_system::transaction_participator* check_participator = &participator.second;
    if (participator_key == check_participator->participator_key()) {
      selected_participator = check_participator;
      break;
    }
  }

  if (selected_participator == nullptr) {
    FWLOGWARNING("Transaction {} commit for participator {}, participator not found",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  if (selected_participator->participator_status() ==
      atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }
  if (selected_participator->participator_status() !=
      atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED) {
    selected_participator->set_participator_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
    has_changed = true;
  }

  bool all_resolved = true;
  for (const auto& participator : *all_participators) {
    if (participator.second.participator_status() !=
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED) {
      all_resolved = false;
      break;
    }
  }

  int ret = 0;

  // 所有的角色都已经处理完事务，可以删除了
  if (all_resolved) {
    if (trans->data_object.metadata().memory_only()) {
      ret = 0;
    } else {
      // 删除前先等待该记录所有未完成的 IO（拉取/保存）结束，避免晚到的 IO 在删除后复活记录；
      // 记录已被并发删除时继续幂等删除
      if (lru_caches_.is_io_task_running(trans->data_object.metadata().transaction_uuid())) {
        ret = RPC_AWAIT_CODE_RESULT(lru_caches_.await_io_task(ctx, trans->data_object.metadata().transaction_uuid()));
      }
      if (ret != 0) {
        FWLOGERROR("Transaction {} commit participator {} and drain pending IO failed, res: {}({})",
                   trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                   protobuf_mini_dumper_get_error_msg(ret));
        RPC_RETURN_CODE(ret);
      }

      ret = RPC_AWAIT_CODE_RESULT(
          rpc::db::distribute_transaction::remove_all(ctx, get_transaction_zone_id(trans->data_object.metadata()),
                                                      trans->data_object.metadata().transaction_uuid()));
      if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
        ret = 0;
      }
    }

    if (ret != 0) {
      FWLOGERROR("Transaction {} commit participator {} and remove transaction failed, res: {}({})",
                 trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
    } else {
      lru_caches_.remove_cache(trans->data_object.metadata().transaction_uuid());
    }
  } else if (has_changed && !trans->data_object.metadata().memory_only()) {
    ret = RPC_AWAIT_CODE_RESULT(save(ctx, trans));
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }
    if (ret != 0) {
      FWLOGERROR("Transaction {} commit participator {} but save failed, res: {}({})",
                 trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_reject(rpc::context& ctx, transaction_ptr_type& trans,
                                                      const std::string& participator_key) {
  if (!trans) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto* all_participators = trans->data_object.mutable_participators();
  if (all_participators == nullptr) {
    FWLOGWARNING("Transaction {} reject for participator, has no participators",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  atfw::distributed_system::transaction_participator* selected_participator = nullptr;
  bool has_changed = false;
  for (auto& participator : *all_participators) {
    atfw::distributed_system::transaction_participator* check_participator = &participator.second;
    if (participator_key == check_participator->participator_key()) {
      selected_participator = check_participator;
      break;
    }
  }

  if (selected_participator == nullptr) {
    FWLOGWARNING("Transaction {} reject for participator {}, participator not found",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  if (selected_participator->participator_status() ==
      atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_FINISHED);
  }
  if (selected_participator->participator_status() !=
      atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
    selected_participator->set_participator_status(
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
    has_changed = true;
  }

  bool all_resolved = true;
  for (const auto& participator : *all_participators) {
    if (participator.second.participator_status() !=
        atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED) {
      all_resolved = false;
      break;
    }
  }

  int ret = 0;

  // 所有的角色都已经处理完事务，可以删除了
  if (all_resolved) {
    if (trans->data_object.metadata().memory_only()) {
      ret = 0;
    } else {
      // 删除前先等待该记录所有未完成的 IO（拉取/保存）结束，避免晚到的 IO 在删除后复活记录；
      // 记录已被并发删除时继续幂等删除
      if (lru_caches_.is_io_task_running(trans->data_object.metadata().transaction_uuid())) {
        ret = RPC_AWAIT_CODE_RESULT(lru_caches_.await_io_task(ctx, trans->data_object.metadata().transaction_uuid()));
      }
      if (ret != 0) {
        FWLOGERROR("Transaction {} reject participator {} and drain pending IO failed, res: {}({})",
                   trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                   protobuf_mini_dumper_get_error_msg(ret));
        RPC_RETURN_CODE(ret);
      }

      ret = RPC_AWAIT_CODE_RESULT(
          rpc::db::distribute_transaction::remove_all(ctx, get_transaction_zone_id(trans->data_object.metadata()),
                                                      trans->data_object.metadata().transaction_uuid()));
      if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
        ret = 0;
      }
    }

    if (ret != 0) {
      FWLOGERROR("Transaction {} reject participator {} and remove transaction failed, res: {}({})",
                 trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
    } else {
      lru_caches_.remove_cache(trans->data_object.metadata().transaction_uuid());
    }
  } else if (has_changed && !trans->data_object.metadata().memory_only()) {
    ret = RPC_AWAIT_CODE_RESULT(save(ctx, trans));
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }
    if (ret != 0) {
      FWLOGERROR("Transaction {} reject participator {} but save failed, res: {}({})",
                 trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_commit(rpc::context& ctx, transaction_ptr_type& trans) {
  if (!trans) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  atfw::distributed_system::transaction_metadata* metadata = trans->data_object.mutable_metadata();

  if (metadata->status() > atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    FWLOGWARNING("Transaction {} is already finished with status: {}, skip commit", metadata->transaction_uuid(),
                 static_cast<int>(metadata->status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  metadata->set_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
  protobuf_copy_message(*metadata->mutable_finish_timepoint(),
                        protobuf_from_system_clock(util::time::time_utility::now()));

  if (metadata->memory_only()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int ret = RPC_AWAIT_CODE_RESULT(save(ctx, trans));
  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  if (ret != 0) {
    FWLOGERROR("Transaction {} commit save failed, res: {}({})", metadata->transaction_uuid(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_reject(rpc::context& ctx, transaction_ptr_type& trans) {
  if (!trans) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  atfw::distributed_system::transaction_metadata* metadata = trans->data_object.mutable_metadata();

  if (metadata->status() > atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    FWLOGWARNING("Transaction {} is already finished with status: {}, skip reject", metadata->transaction_uuid(),
                 static_cast<int>(metadata->status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  metadata->set_status(atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
  protobuf_copy_message(*metadata->mutable_finish_timepoint(),
                        protobuf_from_system_clock(util::time::time_utility::now()));

  if (metadata->memory_only()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int ret = RPC_AWAIT_CODE_RESULT(save(ctx, trans));
  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }
  if (ret != 0) {
    FWLOGERROR("Transaction {} reject save failed, res: {}({})", metadata->transaction_uuid(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_remove(rpc::context& ctx,
                                                      const atfw::distributed_system::transaction_metadata& metadata) {
  if (metadata.transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  int ret = 0;
  if (!metadata.memory_only()) {
    // 删除前先等待该记录所有未完成的 IO 结束，避免删除后晚到的 IO 复活记录；
    // 强制删除语义下即使等待 IO 结束失败也继续删除（缓存会被 remove_cache 置 removed 并禁止再写回）
    rpc::result_code_type::value_type drain_ret = 0;
    if (lru_caches_.is_io_task_running(metadata.transaction_uuid())) {
      drain_ret = RPC_AWAIT_CODE_RESULT(lru_caches_.await_io_task(ctx, metadata.transaction_uuid()));
    }
    if (drain_ret != 0) {
      FWLOGWARNING("Transaction {} drain pending IO before remove failed, res: {}({}), remove anyway",
                   metadata.transaction_uuid(), drain_ret, protobuf_mini_dumper_get_error_msg(drain_ret));
    }

    ret = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::remove_all(ctx, get_transaction_zone_id(metadata),
                                                                            metadata.transaction_uuid()));
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = 0;
    }
  }

  if (ret != 0) {
    FWLOGERROR("Transaction {} remove failed, res: {}({})", metadata.transaction_uuid(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
  } else {
    lru_caches_.remove_cache(metadata.transaction_uuid());
  }

  RPC_RETURN_CODE(ret);
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
size_t transaction_manager::get_lru_size_for_unit_test() noexcept {
  // 只统计未被 remove_cache/clear 置为墓碑的条目，与 get_cache 的可见性口径一致。
  size_t active = 0;
  for (auto iter = lru_caches_.begin(); iter != lru_caches_.end(); ++iter) {
    if (iter->second && !iter->second->removed) {
      ++active;
    }
  }
  return active;
}

void transaction_manager::clear_lru_for_unit_test() noexcept { lru_caches_.clear(); }
#endif
