// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-04-29 11:47:42

#include "task_action_notify_client_started.h"

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

ORBIT_CONTROLLER_SERVICE_API task_action_notify_client_started::task_action_notify_client_started(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_CONTROLLER_SERVICE_API task_action_notify_client_started::~task_action_notify_client_started() {}

ORBIT_CONTROLLER_SERVICE_API const char* task_action_notify_client_started::name() const {
  return "task_action_notify_client_started";
}

ORBIT_CONTROLLER_SERVICE_API task_action_notify_client_started::result_type
task_action_notify_client_started::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  int32_t rpc_result = RPC_AWAIT_CODE_RESULT(
      orbit_controller_manager::me()->handle_notify_client_started(get_shared_context(), req_body));
  TASK_ACTION_RETURN_CODE(rpc_result);
}

ORBIT_CONTROLLER_SERVICE_API int task_action_notify_client_started::on_success() { return get_result(); }

ORBIT_CONTROLLER_SERVICE_API int task_action_notify_client_started::on_failed() { return get_result(); }
