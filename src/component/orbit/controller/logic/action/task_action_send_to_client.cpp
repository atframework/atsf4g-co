// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-05-11 15:53:52

#include "logic/action/task_action_send_to_client.h"

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
#include <utility>

task_action_send_to_client::task_action_send_to_client(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_send_to_client::~task_action_send_to_client() {}

const char* task_action_send_to_client::name() const { return "task_action_send_to_client"; }

task_action_send_to_client::result_type task_action_send_to_client::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  if (is_stream_rpc()) {
    disable_response_message();
  }
  TASK_ACTION_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      orbit_controller_manager::me()->handle_send_to_client(get_shared_context(), req_body, rsp_body)));
}

int task_action_send_to_client::on_success() { return get_result(); }

int task_action_send_to_client::on_failed() { return get_result(); }
