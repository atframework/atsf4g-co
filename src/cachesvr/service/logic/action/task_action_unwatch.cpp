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

#include "task_action_unwatch.h"

task_action_unwatch::task_action_unwatch(dispatcher_start_data_type&& param) : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_unwatch::~task_action_unwatch() {}

const char* task_action_unwatch::name() const { return "task_action_unwatch"; }

task_action_unwatch::result_type task_action_unwatch::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  int ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
  for (int i = 0; i < req_body.unwatch_keys_size(); ++i) {
    const PROJECT_NAMESPACE_ID::object_cache_key& key = req_body.unwatch_keys(i);
    cache_group_base* group = cache_group_manager::me()->get_group(key.cache_type());
    if (nullptr == group) {
      FWLOGERROR("invalid cache group {}", static_cast<uint32_t>(key.cache_type()));
      ret = PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
      continue;
    }

    group->unwatch(key, req_body.watcher());
  }

  TASK_ACTION_RETURN_CODE(ret);
}

int task_action_unwatch::on_success() { return get_result(); }

int task_action_unwatch::on_failed() { return get_result(); }
