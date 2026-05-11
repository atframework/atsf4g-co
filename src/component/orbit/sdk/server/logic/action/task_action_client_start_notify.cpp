// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-05-11 15:53:52

#include "task_action_client_start_notify.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/server_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>
#include <logic/orbit_server_manager.h>

ORBIT_SERVER_SERVICE_API task_action_client_start_notify::task_action_client_start_notify(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_SERVER_SERVICE_API task_action_client_start_notify::~task_action_client_start_notify() {}

ORBIT_SERVER_SERVICE_API const char* task_action_client_start_notify::name() const {
  return "task_action_client_start_notify";
}

ORBIT_SERVER_SERVICE_API task_action_client_start_notify::result_type task_action_client_start_notify::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  RPC_AWAIT_IGNORE_RESULT(orbit_server_manager::me()->handle_client_start_notify(get_shared_context(), req_body));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_SERVER_SERVICE_API int task_action_client_start_notify::on_success() { return get_result(); }

ORBIT_SERVER_SERVICE_API int task_action_client_start_notify::on_failed() { return get_result(); }
