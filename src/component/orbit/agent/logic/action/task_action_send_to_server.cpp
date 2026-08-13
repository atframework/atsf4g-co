// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-05-11 15:53:52

#include "logic/action/task_action_send_to_server.h"

#include <logic/orbit_agent_manager.h>

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/client_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <rpc/agenttocontrollerservice/agenttocontrollerservice.atfw.gen.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>
#include <utility>

task_action_send_to_server::task_action_send_to_server(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_send_to_server::~task_action_send_to_server() {}

const char* task_action_send_to_server::name() const { return "task_action_send_to_server"; }

task_action_send_to_server::result_type task_action_send_to_server::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      orbit_agent_manager::me()->handle_send_to_server(get_shared_context(), req_body, rsp_body)));
}

int task_action_send_to_server::on_success() { return get_result(); }

int task_action_send_to_server::on_failed() { return get_result(); }
