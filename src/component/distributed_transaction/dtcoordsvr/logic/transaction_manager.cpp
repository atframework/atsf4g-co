// Copyright 2022 atframework
// Created by owent, on 2022-02-25

#include "logic/transaction_manager.h"

#include <common/string_oprs.h>
#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
#  include <testing/unit_test_case_cleanup.h>
#endif

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

void transaction_manager::stop() { is_exiting_ = true; }

void transaction_manager::cleanup() { lru_caches_.clear(); }

rpc::result_code_type transaction_manager::await_io_task(rpc::context& ctx, const std::string& transaction_uuid) {
  while (auto data = lru_caches_.get_cache(transaction_uuid, false)) {
    if (task_type_trait::empty(data->io_task)) {
      break;
    }
    if (task_type_trait::is_exiting(data->io_task)) {
      task_type_trait::reset_task(data->io_task);
      break;
    }
    if (task_type_trait::get_task_id(data->io_task) == ctx.get_task_context().task_id) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_CALL_NOT_READY);
    }
    int result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, data->io_task));
    if (result != 0) {
      RPC_RETURN_CODE(result);
    }
    // IO 自行清除句柄。醒来后重新查找，不能清除其他等待者刚登记的新任务。
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type transaction_manager::run_io_task(rpc::context& ctx, transaction_ptr_type data,
                                                       std::function<rpc::result_code_type(rpc::context&)> action,
                                                       bool invalidate_on_error) {
  auto invoked = rpc::async_invoke(
      ctx, "transaction_manager.io",
      [data, action = std::move(action), invalidate_on_error](rpc::context& subctx) -> rpc::result_code_type {
        int result = PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING;
        auto release = gsl::finally([data, invalidate_on_error, &result, task_id = subctx.get_task_context().task_id] {
          if (task_type_trait::get_task_id(data->io_task) == task_id) {
            task_type_trait::reset_task(data->io_task);
          }

          if (transaction_manager::is_instance_destroyed()) {
            return;
          }

          auto& lru_caches = transaction_manager::me()->lru_caches_;

          // 调用者超时不移除在途 IO。只有子任务收尾时才清除失败的数据或空占位。
          if (((invalidate_on_error && result != 0) || data->data_object.metadata().transaction_uuid().empty()) &&
              lru_caches.get_cache(data->data_key, false) == data) {
            lru_caches.remove_cache(data->data_key);
          }
        });
        result = RPC_AWAIT_CODE_RESULT(action(subctx));
        RPC_RETURN_CODE(result);
      });
  if (invoked.is_error()) {
    if ((invalidate_on_error || data->data_object.metadata().transaction_uuid().empty()) &&
        lru_caches_.get_cache(data->data_key, false) == data) {
      lru_caches_.remove_cache(data->data_key);
    }
    RPC_RETURN_CODE(*invoked.get_error());
  }
  auto task = *invoked.get_success();
  if (!task_type_trait::is_exiting(task)) {
    data->io_task = task;
  }
  int result = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, task));
  RPC_RETURN_CODE(result == 0 ? task_type_trait::get_result(task) : result);
}

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
  // 0 表示不限容量（仅按 lru_expired_duration 过期淘汰）：显式配置 0 时不会每个 tick 都清空缓存
  const bool has_capacity_limit = max_count > 0;
  for (auto iter = lru_caches_.begin(); iter != lru_caches_.end();) {
    if (!iter->second) {
      iter = lru_caches_.erase(iter);
      ++ret;
      continue;
    }

    const bool is_expired = now > iter->second->last_visit_timepoint + timeout_duration;
    const bool is_over_capacity = has_capacity_limit && lru_caches_.size() > max_count;
    if (!is_over_capacity && !is_expired) {
      // LRU 按访问时间排序，之后的缓存更新，都不会到期
      break;
    }

    // 同 UUID 的操作通过此 io_task 串行执行，IO 完成前不能淘汰它的缓存入口。
    if (!task_type_trait::empty(iter->second->io_task) && !task_type_trait::is_exiting(iter->second->io_task)) {
      ++iter;
      continue;
    }
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

rpc::result_code_type transaction_manager::save(rpc::context& ctx, transaction_ptr_type& input) {
  transaction_ptr_type data = input;
  if (!data) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (data->removed) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  int result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, data->data_key));
  if (result != 0) {
    RPC_RETURN_CODE(result);
  }
  if (data->removed || lru_caches_.get_cache(data->data_key, false) != data) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  if (data->data_object.metadata().memory_only()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(run_io_task(
      ctx, data,
      [data](rpc::context& subctx) -> rpc::result_code_type {
        uint64_t data_version = static_cast<uint64_t>(data->data_version);
        const auto& input = data->data_object;
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> storage{subctx};
        storage->set_zone_id(get_transaction_zone_id(input.metadata()));
        storage->set_transaction_uuid(input.metadata().transaction_uuid());
        if (!storage->mutable_blob_data()->PackFrom(input)) {
          FCTXLOGERROR(subctx, "Serialize transaction_blob_storage failed, {}",
                       storage->blob_data().InitializationErrorString());
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
        }
        int result = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::replace(subctx, storage, data_version));
        if (result < 0) {
          RPC_RETURN_CODE(result);
        }
        data->data_version = static_cast<int64_t>(data_version);

        // 创建已设置 TTL；刷新失败不改变此次持久化成功的结果。
        int ttl_result = RPC_AWAIT_CODE_RESULT(refresh_transaction_ttl(subctx, input));
        if (ttl_result < 0) {
          FCTXLOGERROR(subctx, "Refresh transaction {} TTL failed after save, res: {}({})",
                       input.metadata().transaction_uuid(), ttl_result, protobuf_mini_dumper_get_error_msg(ttl_result));
        }
        RPC_RETURN_CODE(result);
      },
      true)));
}

rpc::result_code_type transaction_manager::create_transaction(
    rpc::context& ctx, atfw::distributed_system::transaction_blob_storage&& storage) {
  if (is_exiting_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
  }
  if (storage.metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  if (storage.participators().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  const std::string transaction_uuid = storage.metadata().transaction_uuid();
  int wait_result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, transaction_uuid));
  if (wait_result != 0 || is_exiting_) {
    RPC_RETURN_CODE(is_exiting_ ? PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN : wait_result);
  }
  if (storage.metadata().memory_only() && lru_caches_.get_cache(storage.metadata().transaction_uuid())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
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

  auto transaction_cache_ptr = lru_caches_.get_cache(transaction_uuid);
  if (!transaction_cache_ptr) {
    transaction_cache_ptr =
        atfw::component::memory::stl::make_strong_rc<transaction_lru_map_type::value_cache_type>(transaction_uuid);
    if (!transaction_cache_ptr) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    lru_caches_.set_cache(transaction_cache_ptr);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(run_io_task(
      ctx, transaction_cache_ptr,
      [transaction_cache_ptr, storage = std::move(storage)](rpc::context& ctx) mutable -> rpc::result_code_type {
        uint64_t db_version = 0;
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> db_data{ctx};
        db_data->set_zone_id(get_transaction_zone_id(storage.metadata()));
        db_data->set_transaction_uuid(storage.metadata().transaction_uuid());
        if (false == db_data->mutable_blob_data()->PackFrom(storage)) {
          FCTXLOGERROR(ctx, "Serialize transaction_blob_storage failed, {}",
                       db_data->blob_data().InitializationErrorString());
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
        }

        rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
        if (!storage.metadata().memory_only()) {
          // 相同 UUID 的重放不能覆盖已持久化的终态或参与者确认；由 DB 原子判定是否首次创建。
          ret = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::insert(ctx, db_data, &db_version));
          if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS) {
            // 插入成功后 TTL 设置可能失败或响应丢失；重放补设原记录的 TTL，不覆盖数据。
            ret = RPC_AWAIT_CODE_RESULT(
                rpc::db::distribute_transaction::get_all(ctx, get_transaction_zone_id(storage.metadata()),
                                                         storage.metadata().transaction_uuid(), *db_data, db_version));
            if (ret < 0) {
              RPC_RETURN_CODE(ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND
                                  ? PROJECT_NAMESPACE_ID::err::EN_DB_KEY_EXISTS
                                  : ret);
            }
            rpc::context::message_holder<atfw::distributed_system::transaction_blob_storage> existing(ctx);
            if (!db_data->blob_data().UnpackTo(&*existing)) {
              RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
            }
            if (existing->metadata().transaction_uuid() != transaction_cache_ptr->data_key) {
              RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
            }
            RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(refresh_transaction_ttl(ctx, *existing)));
          }
          if (ret < 0) {
            FCTXLOGERROR(ctx, "rpc::db::distribute_transaction::insert({}) failed, res: {}({})",
                         storage.metadata().transaction_uuid(), ret, protobuf_mini_dumper_get_error_msg(ret));
            RPC_RETURN_CODE(ret);
          }

          ret = RPC_AWAIT_CODE_RESULT(refresh_transaction_ttl(ctx, storage));
          if (ret < 0) {
            FCTXLOGERROR(ctx, "Set transaction {} TTL failed after create, res: {}({})",
                         storage.metadata().transaction_uuid(), ret, protobuf_mini_dumper_get_error_msg(ret));
            rpc::result_code_type::value_type remove_result =
                RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::remove_all(
                    ctx, get_transaction_zone_id(storage.metadata()), storage.metadata().transaction_uuid()));
            if (remove_result < 0 && remove_result != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
              FCTXLOGERROR(ctx, "Remove transaction {} after TTL failure failed, res: {}({})",
                           storage.metadata().transaction_uuid(), remove_result,
                           protobuf_mini_dumper_get_error_msg(remove_result));
            }
            RPC_RETURN_CODE(ret);
          }
        }

        // 创建期间可能停服；完成 DB TTL 设置后返回错误，由 IO 收尾清除空占位。
        if (transaction_manager::is_instance_destroyed() || transaction_manager::me()->is_exiting_) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
        }
        transaction_cache_ptr->data_version = static_cast<int64_t>(db_version);
        protobuf_move_message(transaction_cache_ptr->data_object, std::move(storage));

        RPC_RETURN_CODE(ret);
      })));
}

rpc::result_code_type transaction_manager::mutable_transaction(
    rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata, transaction_ptr_type& out) {
  const std::string transaction_uuid = metadata.transaction_uuid();
  const bool memory_only = metadata.memory_only();
  const uint32_t zone_id = get_transaction_zone_id(metadata);
  out.reset();
  if (is_exiting_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
  }
  if (transaction_uuid.empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }
  int ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, transaction_uuid));
  if (ret != 0 || is_exiting_) {
    RPC_RETURN_CODE(is_exiting_ ? PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN : ret);
  }
  auto data = lru_caches_.get_cache(transaction_uuid);
  if (!data && !memory_only) {
    data = atfw::component::memory::stl::make_strong_rc<transaction_lru_map_type::value_cache_type>(transaction_uuid);
    if (!data) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    lru_caches_.set_cache(data);
    ret = RPC_AWAIT_CODE_RESULT(run_io_task(
        ctx, data,
        [data, zone_id](rpc::context& subctx) -> rpc::result_code_type {
          const auto& key = data->data_key;
          auto& output = data->data_object;
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
            FCTXLOGERROR(subctx, "ParseFromString transaction_blob_storage failed, {}", error_msg);
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
          }
          if (output.metadata().transaction_uuid() != key) {
            FCTXLOGERROR(subctx, "Transaction record {} contains mismatched UUID {}", key,
                         output.metadata().transaction_uuid());
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
          }

          data->data_version = static_cast<int64_t>(data_version);

          RPC_RETURN_CODE(sub_ret);
        },
        true));
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }
    if (ret == 0) {
      ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, transaction_uuid));
    }
  }
  if (is_exiting_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
  }
  if (ret != 0) {
    RPC_RETURN_CODE(ret);
  }
  if (!data || data->removed || lru_caches_.get_cache(transaction_uuid, false) != data) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  // 超时且未提交的视为事务失败。
  if (data->data_object.metadata().status() <= atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    auto now = util::time::time_utility::now();
    if (now >
        protobuf_to_system_clock(data->data_object.metadata().expire_timepoint()) + get_transaction_expire_grace()) {
      data->data_object.mutable_metadata()->set_status(
          atfw::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
      protobuf_copy_message(*data->data_object.mutable_metadata()->mutable_finish_timepoint(),
                            protobuf_from_system_clock(now));
      ret = RPC_AWAIT_CODE_RESULT(save(ctx, data));
    }
  }

  if (ret == 0) {
    ret = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, transaction_uuid));
  }
  if (is_exiting_ || ret != 0) {
    RPC_RETURN_CODE(is_exiting_ ? PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN : ret);
  }
  if (data->removed || lru_caches_.get_cache(transaction_uuid, false) != data) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  out = std::move(data);
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_commit(rpc::context& ctx, transaction_ptr_type& input,
                                                      const std::string& participator_key) {
  transaction_ptr_type trans = input;
  if (!trans || trans->removed) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  int wait_result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, trans->data_key));
  if (wait_result != 0) {
    RPC_RETURN_CODE(wait_result);
  }
  if (trans->removed || lru_caches_.get_cache(trans->data_key, false) != trans) {
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
    ret = RPC_AWAIT_CODE_RESULT(remove_transaction(ctx, trans->data_object.metadata(), trans));

    if (ret != 0) {
      FWLOGERROR("Transaction {} commit participator {} and remove transaction failed, res: {}({})",
                 trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
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

rpc::result_code_type transaction_manager::try_reject(rpc::context& ctx, transaction_ptr_type& input,
                                                      const std::string& participator_key) {
  transaction_ptr_type trans = input;
  if (!trans || trans->removed) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  int wait_result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, trans->data_key));
  if (wait_result != 0) {
    RPC_RETURN_CODE(wait_result);
  }
  if (trans->removed || lru_caches_.get_cache(trans->data_key, false) != trans) {
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
    ret = RPC_AWAIT_CODE_RESULT(remove_transaction(ctx, trans->data_object.metadata(), trans));

    if (ret != 0) {
      FWLOGERROR("Transaction {} reject participator {} and remove transaction failed, res: {}({})",
                 trans->data_object.metadata().transaction_uuid(), participator_key, ret,
                 protobuf_mini_dumper_get_error_msg(ret));
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

rpc::result_code_type transaction_manager::try_commit(rpc::context& ctx, transaction_ptr_type& input) {
  transaction_ptr_type trans = input;
  if (!trans || trans->removed) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  int wait_result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, trans->data_key));
  if (wait_result != 0) {
    RPC_RETURN_CODE(wait_result);
  }
  if (trans->removed || lru_caches_.get_cache(trans->data_key, false) != trans) {
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

rpc::result_code_type transaction_manager::try_reject(rpc::context& ctx, transaction_ptr_type& input) {
  transaction_ptr_type trans = input;
  if (!trans || trans->removed) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  int wait_result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, trans->data_key));
  if (wait_result != 0) {
    RPC_RETURN_CODE(wait_result);
  }
  if (trans->removed || lru_caches_.get_cache(trans->data_key, false) != trans) {
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

  int ret = RPC_AWAIT_CODE_RESULT(remove_transaction(ctx, metadata, nullptr));
  if (ret != 0) {
    FCTXLOGERROR(ctx, "Transaction {} remove failed, res: {}({})", metadata.transaction_uuid(), ret,
                 protobuf_mini_dumper_get_error_msg(ret));
  }
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::remove_transaction(
    rpc::context& ctx, const atfw::distributed_system::transaction_metadata& metadata,
    transaction_ptr_type expected_cache) {
  const std::string transaction_uuid = metadata.transaction_uuid();
  bool memory_only = metadata.memory_only();
  int result = RPC_AWAIT_CODE_RESULT(await_io_task(ctx, transaction_uuid));
  if (result != 0) {
    RPC_RETURN_CODE(result);
  }
  auto data = lru_caches_.get_cache(transaction_uuid, false);
  if (expected_cache && (expected_cache->removed || data != expected_cache)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }
  // 如果本地有数据一定要以本地数据为准
  uint32_t zone_id = 0;
  if (data) {
    memory_only = data->data_object.metadata().memory_only();
    zone_id = get_transaction_zone_id(data->data_object.metadata());
  } else {
    zone_id = get_transaction_zone_id(metadata);
  }
  if (memory_only) {
    lru_caches_.remove_cache(transaction_uuid);
    RPC_RETURN_CODE(0);
  }
  if (!data) {
    data = atfw::component::memory::stl::make_strong_rc<transaction_lru_map_type::value_cache_type>(transaction_uuid);
    if (!data) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }
    lru_caches_.set_cache(data);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      run_io_task(ctx, data, [this, data, zone_id](rpc::context& subctx) -> rpc::result_code_type {
        int result =
            RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::remove_all(subctx, zone_id, data->data_key));
        if (result == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
          result = 0;
        }
        if (result == 0 && lru_caches_.get_cache(data->data_key, false) == data) {
          lru_caches_.remove_cache(data->data_key);
        }
        RPC_RETURN_CODE(result);
      })));
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
size_t transaction_manager::get_lru_size_for_unit_test() noexcept {
  // remove_cache/clear 会把条目真正移出池，池内不存在 removed 条目，可见条目数即池大小
  return lru_caches_.size();
}

void transaction_manager::clear_lru_for_unit_test() noexcept { lru_caches_.clear(); }

void transaction_manager::reset_for_unit_test() noexcept {
  clear_lru_for_unit_test();
  is_exiting_ = false;
}

namespace {
// 用例边界自动清理：协调者是进程级单例，LRU 与退出标志跨用例存活；stop() 置位后不复位会让同进程
// 后续用例全部拿到 EN_SYS_SERVER_SHUTDOWN。
// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
static const bool transaction_manager_case_cleanup_registered ATFW_EXPLICIT_UNUSED_ATTR = [] {
  server_frame_unit_test_register_case_cleanup("distributed_transaction.transaction_manager",
                                               kUnitTestCaseCleanupLevelBusiness, []() {
                                                 if (!transaction_manager::is_instance_destroyed()) {
                                                   transaction_manager::me()->reset_for_unit_test();
                                                 }
                                               });
  return true;
}();
}  // namespace
#endif
