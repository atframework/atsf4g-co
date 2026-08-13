// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-05-11 10:55:12

#include "logic/action/task_action_client_agent_heartbeat.h"

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
#include <utility>

ORBIT_SERVER_SERVICE_API task_action_client_agent_heartbeat::task_action_client_agent_heartbeat(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_SERVER_SERVICE_API task_action_client_agent_heartbeat::~task_action_client_agent_heartbeat() {}

ORBIT_SERVER_SERVICE_API const char* task_action_client_agent_heartbeat::name() const {
  return "task_action_client_agent_heartbeat";
}

ORBIT_SERVER_SERVICE_API task_action_client_agent_heartbeat::result_type
task_action_client_agent_heartbeat::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      orbit_server_manager::me()->handle_client_agent_heartbeat_notify(get_shared_context(), req_body)));
}

ORBIT_SERVER_SERVICE_API int task_action_client_agent_heartbeat::on_success() { return get_result(); }

ORBIT_SERVER_SERVICE_API int task_action_client_agent_heartbeat::on_failed() { return get_result(); }
