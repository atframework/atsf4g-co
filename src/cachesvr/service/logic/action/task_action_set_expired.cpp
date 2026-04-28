// Copyright 2026 atframework
// Created by owent with generate-for-pb.py at 2020-12-15 16:29:08
//

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <logic/cache_group.h>
#include <logic/cache_group_manager.h>

#include "task_action_set_expired.h"

task_action_set_expired::task_action_set_expired(dispatcher_start_data_type &&param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_set_expired::~task_action_set_expired() {}

const char *task_action_set_expired::name() const { return "task_action_set_expired"; }

task_action_set_expired::result_type task_action_set_expired::operator()() {
  const rpc_request_type &req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  for (int i = 0; i < req_body.expired_keys_size(); ++i) {
    const PROJECT_NAMESPACE_ID::object_cache_key &key = req_body.expired_keys(i);
    cache_group_base *group = cache_group_manager::me()->get_group(key.cache_type());
    if (nullptr == group) {
      FWLOGERROR("invalid cache group {}", static_cast<uint32_t>(key.cache_type()));
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      continue;
    }

    // 如果没有订阅没有数据，直接忽略即可
    std::shared_ptr<cache_object_base> cache_object = group->get_cache(key);
    if (!cache_object) {
      continue;
    }

    RPC_AWAIT_IGNORE_RESULT(cache_object->set_cache_expired(get_shared_context()));
  }

  TASK_ACTION_RETURN_CODE(ret);
}

int task_action_set_expired::on_success() { return get_result(); }

int task_action_set_expired::on_failed() { return get_result(); }
