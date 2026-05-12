// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-05-11 15:53:52

#include "task_action_notify_client_exit.h"

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

ORBIT_CONTROLLER_SERVICE_API task_action_notify_client_exit::task_action_notify_client_exit(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_CONTROLLER_SERVICE_API task_action_notify_client_exit::~task_action_notify_client_exit() {}

ORBIT_CONTROLLER_SERVICE_API const char* task_action_notify_client_exit::name() const {
  return "task_action_notify_client_exit";
}

ORBIT_CONTROLLER_SERVICE_API task_action_notify_client_exit::result_type task_action_notify_client_exit::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      orbit_controller_manager::me()->handle_notify_client_exit(get_shared_context(), req_body, rsp_body)));
}

ORBIT_CONTROLLER_SERVICE_API int task_action_notify_client_exit::on_success() { return get_result(); }

ORBIT_CONTROLLER_SERVICE_API int task_action_notify_client_exit::on_failed() { return get_result(); }
