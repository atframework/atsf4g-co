// Copyright 2026 atframework
#include "logic/user_cache_group.h"

#include <logic/cache_group_manager.h>
#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/lobby/lobbysvrservice.atfw.gen.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/user/user_basic.h>
#include <utility/protobuf_mini_dumper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.protocol.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

user_cache_group::user_cache_group(cache_group_manager &manager)
    : super(manager, user_cache_group::pull_user_cache_fn, user_cache_group::pack_user_cache_fn,
            user_cache_group::update_meta_user_cache_fn) {}

int user_cache_group::tick(time_t now, int64_t cachesvr_version) {
  const auto &data_conf = logic_config::me()->get_logic_cfg().cache().data();

  size_t max_recycle_count = data_conf.max_recycle_count_per_tick();
  if (max_recycle_count <= 0) {
    max_recycle_count = 100;
  }

  return super::tick(cachesvr_version, now, data_conf.max_user_cache_number(), data_conf.gc_user_cache_number(),
                     max_recycle_count);
}

rpc::result_code_type user_cache_group::pull_user_cache_fn(::rpc::context &ctx,
                                                           super::pull_data_param_ptr_t &fill_data) {
  if (nullptr == fill_data || fill_data->empty()) {
    RPC_RETURN_CODE(0);
  }

  std::vector<rpc::db::user::table_key_t> user_id_key;
  std::vector<rpc::db::user::batch_get_result_t> pull_user_results;
  user_id_key.reserve(fill_data->size());

  std::vector<rpc::db::login_lock::table_key_t> login_lock_key;
  std::vector<rpc::db::login_lock::batch_get_result_t> pull_login_lock_results;
  std::unordered_map<uint64_t, const rpc::db::login_lock::batch_get_result_t *> pull_login_lock_index;
  login_lock_key.reserve(fill_data->size());

  std::unordered_set<uint64_t> pull_user_id_set;
  pull_user_id_set.reserve(fill_data->size());

  for (auto &fill_key : *fill_data) {
    if (nullptr == fill_key.second) {
      continue;
    }

    user_id_key.emplace_back(fill_key.first.zone_id(), fill_key.first.instance_id());
    if (pull_user_id_set.end() == pull_user_id_set.find(fill_key.first.instance_id())) {
      pull_user_id_set.insert(fill_key.first.instance_id());
      login_lock_key.emplace_back(fill_key.first.instance_id());
    }
  }

  if (user_id_key.empty()) {
    RPC_RETURN_CODE(0);
  }

  int32_t ret = RPC_AWAIT_CODE_RESULT(
      rpc::db::user::batch_partly_get_basic_info(ctx, gsl::make_span(user_id_key), pull_user_results));
  if (ret < 0) {
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      RPC_RETURN_CODE(0);
    }
    FWLOGERROR("rpc::db::user::batch_partly_get_basic_info failed, res: {}({})", ret,
               protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }

  // 在线表
  ret = RPC_AWAIT_CODE_RESULT(
      rpc::db::login_lock::batch_get_all(ctx, gsl::make_span(login_lock_key), pull_login_lock_results));
  if (ret < 0) {
    if (ret == PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
      RPC_RETURN_CODE(0);
    }
    FWLOGERROR("rpc::db::login_lock::batch_get_all failed, res: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }
  pull_login_lock_index.reserve(pull_login_lock_results.size());
  for (const auto &table : pull_login_lock_results) {
    if (table.result < 0 || !table.message) {
      continue;
    }

    pull_login_lock_index[(*table.message)->user_id()] = &table;
  }

  for (const auto &table : pull_user_results) {
    if (table.result < 0 || !table.message) {
      continue;
    }

    PROJECT_NAMESPACE_ID::object_cache_key table_key;

    table_key.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
    table_key.set_zone_id((*table.message)->zone_id());
    table_key.set_instance_id((*table.message)->user_id());

    auto iter_fill = fill_data->find(table_key);
    if (iter_fill == fill_data->end()) {
      continue;
    }

    if (nullptr == iter_fill->second) {
      continue;
    }

    rpc::user::convert_to_client_data(*iter_fill->second, **table.message);
  }

  std::vector<task_type_trait::task_type> pending_tasks;
  pending_tasks.reserve(pull_login_lock_index.size());

  for (const auto &table : pull_user_results) {
    if (table.result < 0 || !table.message) {
      continue;
    }

    PROJECT_NAMESPACE_ID::object_cache_key table_key;

    table_key.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_USER);
    table_key.set_zone_id((*table.message)->zone_id());
    table_key.set_instance_id((*table.message)->user_id());

    auto iter_fill = fill_data->find(table_key);
    if (iter_fill == fill_data->end()) {
      continue;
    }

    if (nullptr == iter_fill->second) {
      continue;
    }

    // 判定是否在线
    auto iter_login_lock = pull_login_lock_index.find((*table.message)->user_id());
    if (iter_login_lock == pull_login_lock_index.end()) {
      continue;
    }
    const auto &login_lock_table = *iter_login_lock->second->message;

    uint64_t destination_server_id = login_lock_table->router_server_id();
    if (destination_server_id == 0) {
      continue;
    }

    auto fill_data_ptr = fill_data;
    // 查询gamesvr
    auto invoke_task = rpc::async_invoke(
        ctx, "user_cache_group::pull_user_cache_fn",
        [fill_data_ptr, table_key, destination_server_id](rpc::context &child_ctx) -> rpc::result_code_type {
          auto req_body = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSObjectCacheGetUserCacheDataReq>(child_ctx);
          auto rsp_body = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSObjectCacheGetUserCacheDataRsp>(child_ctx);
          protobuf_copy_message(*req_body->mutable_key(), table_key);

          auto res = RPC_AWAIT_CODE_RESULT(rpc::lobby::object_cache_get_user_cache_data(
              child_ctx, destination_server_id, table_key.zone_id(), table_key.instance_id(),
              std::to_string(table_key.instance_id()), *req_body, *rsp_body));
          if (res < 0) {
            FWLOGERROR("user {}:{} get_user_cache_data failed.res: {}({})", table_key.zone_id(),
                       table_key.instance_id(), res, protobuf_mini_dumper_get_error_msg(res));
            RPC_RETURN_CODE(res);
          }

          if (PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED == rsp_body->result()) {
            FWLOGINFO("user {}:{} but offline, init by db", table_key.zone_id(), table_key.instance_id());
            RPC_RETURN_CODE(0);
          }

          auto iter_fill_data = fill_data_ptr->find(table_key);
          if (iter_fill_data == fill_data_ptr->end()) {
            RPC_RETURN_CODE(0);
          }

          if (nullptr == iter_fill_data->second) {
            RPC_RETURN_CODE(0);
          }

          update_meta_user_cache_fn(child_ctx, rsp_body->cache_meta(), *iter_fill_data->second);
          RPC_RETURN_CODE(0);
        });

    if (invoke_task.is_success()) {
      if (!task_type_trait::is_exiting(*invoke_task.get_success())) {
        pending_tasks.emplace_back(std::move(*invoke_task.get_success()));
      } else {
        if (task_type_trait::get_result(*invoke_task.get_success()) != 0) {
          FWLOGERROR("invoke_task a task to Get failed.res: {}({})",
                     task_type_trait::get_result(*invoke_task.get_success()),
                     protobuf_mini_dumper_get_error_msg(task_type_trait::get_result(*invoke_task.get_success())));
          continue;
        }
      }
    } else {
      FWLOGERROR("invoke_task a task to Get failed.res: {}({})", *invoke_task.get_error(),
                 protobuf_mini_dumper_get_error_msg(*invoke_task.get_error()));
      continue;
    }
  }

  ret = RPC_AWAIT_CODE_RESULT(rpc::wait_tasks(ctx, pending_tasks));
  if (ret == 0) {
    // check single task result
    for (const auto &task : pending_tasks) {
      ret = task_type_trait::get_result(task);
      if (ret != 0) {
        RPC_RETURN_CODE(ret);
      }
    }
  } else {
    FWLOGERROR("pull_user_cache_fn failed, result: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }

  RPC_RETURN_CODE(ret);
}

void user_cache_group::pack_user_cache_fn(
    rpc::context &ctx, const super::value_type &input,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &output) {
  PROJECT_NAMESPACE_ID::object_cache_content *content = output.Add();
  if (nullptr == content) {
    return;
  }

  PROJECT_NAMESPACE_ID::DCacheApiObjectData convert_data;
  PROJECT_NAMESPACE_ID::DCacheApiObjectData *convert_data_ptr = nullptr;
  if (input.get_data().GetArena() != nullptr) {
    convert_data_ptr =
        google::protobuf::Arena::Create<PROJECT_NAMESPACE_ID::DCacheApiObjectData>(input.get_data().GetArena());
  } else {
    convert_data_ptr = &convert_data;
  }
  if (convert_data_ptr == nullptr) {
    return;
  }
  convert_data_ptr->set_allocated_user_cache(const_cast<PROJECT_NAMESPACE_ID::DUserBasicData *>(&input.get_data()));
  auto defer_clear_data = gsl::finally(
      [&convert_data_ptr]() { ATFW_EXPLICIT_UNUSED_ATTR auto *_ = convert_data_ptr->release_user_cache(); });

  if (rpc::cache_api::pack_cache_content_to_any(ctx, *content->mutable_cache_data(), *convert_data_ptr)) {
    content->set_data_version(input.get_data_version());
  }
}

void user_cache_group::update_meta_user_cache_fn(rpc::context &ctx,
                                                 const PROJECT_NAMESPACE_ID::object_cache_meta &input,
                                                 super::cache_type &output) {
  if (input.cache_meta().type_url().empty()) {
    return;
  }

  auto meta_msg = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DCacheApiMetaData>(ctx);
  if (!rpc::cache_api::unpack_cache_meta_from_any(ctx, *meta_msg, input.cache_meta())) {
    FWLOGERROR("unpack user cache meta failed, got type_url: {}, size: {}, unpack to {}", input.cache_meta().type_url(),
               input.cache_meta().value().size(), meta_msg->GetDescriptor()->full_name());
    return;
  }

  if (meta_msg->has_user_meta()) {
    rpc::cache_api::update_cache_content_from_meta(ctx, output, meta_msg->user_meta());
  }
}
