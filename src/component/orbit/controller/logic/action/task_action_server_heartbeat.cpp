// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-04-30 15:07:18

#include "task_action_server_heartbeat.h"

#include <logic/orbit_controller_manager.h>

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

task_action_server_heartbeat::task_action_server_heartbeat(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_server_heartbeat::~task_action_server_heartbeat() {}

const char* task_action_server_heartbeat::name() const {
  return "task_action_server_heartbeat";
}

task_action_server_heartbeat::result_type task_action_server_heartbeat::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  int32_t rpc_result =
      RPC_AWAIT_CODE_RESULT(orbit_controller_manager::me()->handle_server_heartbeat(get_shared_context(), req_body));
  TASK_ACTION_RETURN_CODE(rpc_result);
}

int task_action_server_heartbeat::on_success() { return get_result(); }

int task_action_server_heartbeat::on_failed() { return get_result(); }
