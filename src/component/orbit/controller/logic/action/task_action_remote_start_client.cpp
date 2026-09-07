// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-09-07 13:38:50

#include "logic/action/task_action_remote_start_client.h"

#include <logic/orbit_controller_manager.h>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/controller_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API task_action_remote_start_client::task_action_remote_start_client(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API task_action_remote_start_client::~task_action_remote_start_client() {}

ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API const char* task_action_remote_start_client::name() const {
  return "task_action_remote_start_client";
}

ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API task_action_remote_start_client::result_type
task_action_remote_start_client::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      orbit_controller_manager::me()->handle_remote_start_client(get_shared_context(), req_body, rsp_body)));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API int task_action_remote_start_client::on_success() {
  return get_result();
}

ATFRAMEWORK_ORBIT_AGENTTOCONTROLLERSERVICE_API int task_action_remote_start_client::on_failed() { return get_result(); }
