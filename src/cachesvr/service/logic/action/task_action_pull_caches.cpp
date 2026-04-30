// Copyright 2026 atframework
// Created by owent with generate-for-pb.py at 2020-12-15 16:29:08
//

#include "logic/action/task_action_pull_caches.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <logic/cache_group.h>
#include <logic/cache_group_manager.h>

#include <unordered_map>

task_action_pull_caches::task_action_pull_caches(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_pull_caches::~task_action_pull_caches() {}

const char* task_action_pull_caches::name() const { return "task_action_pull_caches"; }

task_action_pull_caches::result_type task_action_pull_caches::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  std::unordered_map<uint32_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>*>
      cache_keys_by_type;

  for (int i = 0; i < req_body.pull_keys_size(); ++i) {
    const PROJECT_NAMESPACE_ID::object_cache_pull_key& key = req_body.pull_keys(i);
    if (PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_UNKNOWN == key.cache_key().cache_type() ||
        0 == key.cache_key().instance_id()) {
      continue;
    }

    uint32_t cache_type_id = static_cast<uint32_t>(key.cache_key().cache_type());
    auto iter = cache_keys_by_type.find(cache_type_id);
    if (iter == cache_keys_by_type.end()) {
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>* keys =
          get_shared_context()
              .create<::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key>>();
      if (nullptr == keys) {
        FWLOGERROR("malloc RepeatedPtrField failed");
        continue;
      }

      cache_keys_by_type[cache_type_id] = keys;
      keys->Reserve(req_body.pull_keys_size());
      protobuf_copy_message(*keys->Add(), key);
    } else {
      protobuf_copy_message(*iter->second->Add(), key);
    }
  }

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  for (auto& cache_keys : cache_keys_by_type) {
    if (cache_keys.second->empty()) {
      continue;
    }

    uint32_t cache_type_id = static_cast<uint32_t>(cache_keys.second->Get(0).cache_key().cache_type());
    cache_group_base* group = cache_group_manager::me()->get_group(cache_keys.second->Get(0).cache_key().cache_type());
    if (nullptr == group) {
      FWLOGERROR("invalid cache group {}", cache_type_id);
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      continue;
    }

    // 如果没有订阅没有数据，直接忽略即可
    int res =
        RPC_AWAIT_CODE_RESULT(group->pull_content(get_shared_context(), *cache_keys.second, *rsp_body.mutable_content(),
                                                  req_body.watcher(), get_request_node_id()));
    if (res < 0) {
      FWLOGERROR("pull content failed res: {}({})", res, protobuf_mini_dumper_get_error_msg(res));
      set_response_code(res);
    }
  }

  TASK_ACTION_RETURN_CODE(ret);
}

int task_action_pull_caches::on_success() { return get_result(); }

int task_action_pull_caches::on_failed() { return get_result(); }
