// Copyright 2022 atframework
// Created by owent, on 2022-02-25

#include "logic/transaction_manager.h"

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <memory/object_allocator.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/db/distribute_transaction.h>
#include <rpc/rpc_utils.h>

#include <string>

#define TRANSACTION_RETRY_MAX_TIMES 5

namespace {
static uint32_t get_transaction_zone_id(const atframework::distributed_system::transaction_metadata& metadata) {
  if (metadata.replicate_read_count() > 0 &&
      static_cast<uint32_t>(metadata.replicate_node_server_id_size()) >= metadata.replicate_read_count()) {
    return logic_config::me()->get_local_zone_id();
  }

  return 0;
}
}  // namespace

transaction_manager::transaction_manager() : is_exiting_(false), last_stat_timepoint_(0) {}

int transaction_manager::tick() {
  time_t now = util::time::time_utility::get_now();
  if (last_stat_timepoint_ != now / util::time::time_utility::MINITE_SECONDS) {
    last_stat_timepoint_ = now / util::time::time_utility::MINITE_SECONDS;
    FWLOGWARNING("[STATISTICS]: current transition cache count: {}", lru_caches_.size());
  }

  int ret = 0;
  if (lru_caches_.empty()) {
    return ret;
  }

  time_t timeout_duration = logic_config::me()->get_server_cfg().dtcoordsvr().lru_expired_duration().seconds();
  size_t max_count = logic_config::me()->get_server_cfg().dtcoordsvr().lru_max_cache_count();
  while (!lru_caches_.empty()) {
    if (!lru_caches_.front().second) {
      lru_caches_.pop_front();
      ++ret;
      continue;
    }

    if (lru_caches_.size() <= max_count && now <= lru_caches_.front().second->last_visit_timepoint + timeout_duration) {
      break;
    }

    lru_caches_.pop_front();
    ++ret;
  }

  return ret;
}

rpc::result_code_type transaction_manager::save(rpc::context& ctx, transaction_ptr_type& data) {
  if (data && data->data_object.metadata().memory_only()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(lru_caches_.await_save(
      ctx, data,
      [](rpc::context& subctx, const atframework::distributed_system::transaction_blob_storage& in,
         int64_t* out_version) -> rpc::result_code_type {
        std::string data_version;
        if (nullptr != out_version) {
          data_version = util::log::format("{}", *out_version);
        }
        rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> storage{subctx};
        storage->set_transaction_uuid(in.metadata().transaction_uuid());
        if (false == storage->mutable_blob_data()->PackFrom(in)) {
          FWLOGERROR("Serialize transaction_blob_storage failed, {}", storage->blob_data().InitializationErrorString());
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
        }
        int ret = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::set(
            subctx, storage->zone_id(), storage->transaction_uuid(), storage, data_version));
        if (nullptr != out_version) {
          util::string::str2int(*out_version, data_version.c_str(), data_version.size());
        }

        RPC_RETURN_CODE(ret);
      })));
}

rpc::result_code_type transaction_manager::create_transaction(
    rpc::context& ctx, atframework::distributed_system::transaction_blob_storage&& storage) {
  if (storage.metadata().transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  time_t now = util::time::time_utility::get_now();
  int32_t now_nanos = static_cast<int32_t>(util::time::time_utility::get_now_usec() * 1000);
  storage.mutable_metadata()->mutable_prepare_timepoint()->set_seconds(now);
  storage.mutable_metadata()->mutable_prepare_timepoint()->set_nanos(now_nanos);

  if (storage.metadata().expire_timepoint().seconds() <= now) {
    auto& cfg_value = logic_config::me()->get_server_cfg().dtcoordsvr().transaction_default_timeout();
    if (now_nanos + cfg_value.nanos() > 1000000000) {
      storage.mutable_metadata()->mutable_expire_timepoint()->set_seconds(now + cfg_value.seconds() + 1);
      storage.mutable_metadata()->mutable_expire_timepoint()->set_nanos(now_nanos + cfg_value.nanos() - 1000000000);
    } else {
      storage.mutable_metadata()->mutable_expire_timepoint()->set_seconds(now + cfg_value.seconds());
      storage.mutable_metadata()->mutable_expire_timepoint()->set_nanos(now_nanos + cfg_value.nanos());
    }
  }
  // 配置错误则fallback 10秒过期
  if (storage.metadata().expire_timepoint().seconds() <= now) {
    storage.mutable_metadata()->mutable_expire_timepoint()->set_seconds(now + 10);
    storage.mutable_metadata()->mutable_expire_timepoint()->set_nanos(now_nanos);
  }

  std::string db_version;
  rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> db_data{ctx};
  db_data->set_transaction_uuid(storage.metadata().transaction_uuid());
  if (false == db_data->mutable_blob_data()->PackFrom(storage)) {
    FWLOGERROR("Serialize transaction_blob_storage failed, {}", db_data->blob_data().InitializationErrorString());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
  }

  rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  if (!storage.metadata().memory_only()) {
    ret = RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::set(ctx, db_data->zone_id(),
                                                                     db_data->transaction_uuid(), db_data, db_version));

    if (ret < 0) {
      FWLOGERROR("rpc::db::distribute_transaction::add({}) failed, res: {}({})", storage.metadata().transaction_uuid(),
                 ret, protobuf_mini_dumper_get_error_msg(ret));
      RPC_RETURN_CODE(ret);
    }
  }

  transaction_lru_map_type::cache_ptr_type transaction_cache_ptr =
      atfw::memory::stl::make_strong_rc<transaction_lru_map_type::value_cache_type>(db_data->transaction_uuid());
  protobuf_move_message(transaction_cache_ptr->data_object, std::move(storage));
  lru_caches_.set_cache(transaction_cache_ptr);

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::mutable_transaction(
    rpc::context& ctx, const atframework::distributed_system::transaction_metadata& metadata,
    transaction_ptr_type& out) {
  // 停服时返回nullptr
  if (is_exiting_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
  }

  uint32_t zone_id = get_transaction_zone_id(metadata);
  int ret;
  if (!metadata.memory_only()) {
    ret = RPC_AWAIT_CODE_RESULT(lru_caches_.await_fetch(
        ctx, metadata.transaction_uuid(), out,
        [zone_id](rpc::context& subctx, const std::string& key,
                  atframework::distributed_system::transaction_blob_storage& output,
                  int64_t* out_version) -> rpc::result_code_type {
          std::string data_version;
          rpc::shared_message<PROJECT_NAMESPACE_ID::table_distribute_transaction> storage{subctx};
          int sub_ret =
              RPC_AWAIT_CODE_RESULT(rpc::db::distribute_transaction::get(subctx, zone_id, key, storage, data_version));
          if (sub_ret < 0) {
            RPC_RETURN_CODE(sub_ret);
          }

          if (false == storage->blob_data().UnpackTo(&output)) {
            std::string error_msg = output.InitializationErrorString();
            if (error_msg.empty() && output.GetDescriptor()->full_name() != storage->blob_data().type_url()) {
              error_msg = "type mismatch, expect: " + output.GetDescriptor()->full_name() +
                          " , got: " + storage->blob_data().type_url();
            }
            FWLOGERROR("ParseFromString transaction_blob_storage failed, {}", error_msg);
            RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNPACK);
          }

          if (nullptr != out_version) {
            util::string::str2int(*out_version, data_version.c_str(), data_version.size());
          }

          RPC_RETURN_CODE(sub_ret);
        }));

    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    }
  } else {
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
    out.reset();
  }

  // 超时且未提交的视为事务失败
  if (out && out->data_object.metadata().status() <=
                 atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    if (util::time::time_utility::get_now() > out->data_object.metadata().expire_timepoint().seconds() + 5) {
      out->data_object.mutable_metadata()->set_status(
          atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
    }
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_commit(rpc::context& ctx, transaction_ptr_type& trans,
                                                      const std::string& participator_key) {
  if (!trans) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  auto all_participators = trans->data_object.mutable_participators();
  if (all_participators == NULL) {
    FWLOGWARNING("Transaction {} commit for participator {}, has no participators",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  atframework::distributed_system::transaction_participator* selected_participator = nullptr;
  bool all_resolved = true;
  bool has_changed = false;
  for (auto& participator : *all_participators) {
    atframework::distributed_system::transaction_participator* check_participator = &participator.second;
    if (participator_key == check_participator->participator_key()) {
      selected_participator = check_participator;
    } else if (check_participator->participator_status() <
               atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
      all_resolved = false;
    }
  }

  if (selected_participator == NULL) {
    FWLOGWARNING("Transaction {} commit for participator {}, participator not found",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  if (selected_participator->participator_status() <=
      atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    selected_participator->set_participator_status(
        atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
    has_changed = true;
  }

  int ret = 0;

  // 所有的角色都已经处理完事务，可以删除了
  if (all_resolved) {
    if (trans->data_object.metadata().memory_only()) {
      ret = 0;
    } else {
      ret = RPC_AWAIT_CODE_RESULT(
          rpc::db::distribute_transaction::remove(ctx, get_transaction_zone_id(trans->data_object.metadata()),
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

    // 任意错误都认为是缓存过期，要删除缓存
    if (ret != 0) {
      lru_caches_.remove_cache(trans->data_object.metadata().transaction_uuid());
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

  auto all_participators = trans->data_object.mutable_participators();
  if (all_participators == NULL) {
    FWLOGWARNING("Transaction {} reject for participator, has no participators",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  atframework::distributed_system::transaction_participator* selected_participator = nullptr;
  bool all_resolved = true;
  bool has_changed = false;
  for (auto& participator : *all_participators) {
    atframework::distributed_system::transaction_participator* check_participator = &participator.second;
    if (participator_key == check_participator->participator_key()) {
      selected_participator = check_participator;
    } else if (check_participator->participator_status() <
               atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_FINISHED) {
      all_resolved = false;
    }
  }

  if (selected_participator == NULL) {
    FWLOGWARNING("Transaction {} reject for participator {}, participator not found",
                 trans->data_object.metadata().transaction_uuid(), participator_key);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_TRANSACTION_PARTICIPATOR_NOT_FOUND);
  }

  if (selected_participator->participator_status() <=
      atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    selected_participator->set_participator_status(
        atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
    has_changed = true;
  }

  int ret = 0;

  // 所有的角色都已经处理完事务，可以删除了
  if (all_resolved) {
    if (trans->data_object.metadata().memory_only()) {
      ret = 0;
    } else {
      ret = RPC_AWAIT_CODE_RESULT(
          rpc::db::distribute_transaction::remove(ctx, get_transaction_zone_id(trans->data_object.metadata()),
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

    // 任意错误都认为是缓存过期，要删除缓存
    if (ret != 0) {
      lru_caches_.remove_cache(trans->data_object.metadata().transaction_uuid());
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

  atframework::distributed_system::transaction_metadata* metadata = trans->data_object.mutable_metadata();

  if (metadata->status() > atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    FWLOGERROR("Transaction {} already has status: {}, can not commit", metadata->transaction_uuid(),
               static_cast<int>(metadata->status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  metadata->set_status(atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_COMMITED);
  metadata->mutable_finish_timepoint()->set_seconds(util::time::time_utility::get_now());
  metadata->mutable_finish_timepoint()->set_nanos(
      static_cast<int32_t>(util::time::time_utility::get_now_usec() * 1000));

  if (metadata->memory_only()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int ret = RPC_AWAIT_CODE_RESULT(save(ctx, trans));
  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  // 任意错误都认为是缓存过期，要删除缓存
  if (ret != 0) {
    lru_caches_.remove_cache(metadata->transaction_uuid());
    FWLOGERROR("Transaction {} commit save failed, res: {}({})", metadata->transaction_uuid(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_reject(rpc::context& ctx, transaction_ptr_type& trans) {
  if (!trans) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND);
  }

  atframework::distributed_system::transaction_metadata* metadata = trans->data_object.mutable_metadata();

  if (metadata->status() > atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_PREPARED) {
    FWLOGERROR("Transaction {} already has status: {}, can not reject", metadata->transaction_uuid(),
               static_cast<int>(metadata->status()));
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  metadata->set_status(atframework::distributed_system::EN_DISTRIBUTED_TRANSACTION_STATUS_REJECTED);
  metadata->mutable_finish_timepoint()->set_seconds(util::time::time_utility::get_now());
  metadata->mutable_finish_timepoint()->set_nanos(
      static_cast<int32_t>(util::time::time_utility::get_now_usec() * 1000));

  if (metadata->memory_only()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  int ret = RPC_AWAIT_CODE_RESULT(save(ctx, trans));
  if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    ret = PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND;
  }

  // 任意错误都认为是缓存过期，要删除缓存
  if (ret != 0) {
    lru_caches_.remove_cache(metadata->transaction_uuid());
    FWLOGERROR("Transaction {} reject save failed, res: {}({})", metadata->transaction_uuid(), ret,
               protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_CODE(ret);
}

rpc::result_code_type transaction_manager::try_remove(
    rpc::context& ctx, const atframework::distributed_system::transaction_metadata& metadata) {
  if (metadata.transaction_uuid().empty()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
  }

  int ret = 0;
  if (!metadata.memory_only()) {
    RPC_AWAIT_CODE_RESULT(
        rpc::db::distribute_transaction::remove(ctx, get_transaction_zone_id(metadata), metadata.transaction_uuid()));
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
