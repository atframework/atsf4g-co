// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-10 20:11:30

#include "logic/action/task_action_send_message.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

task_action_send_message::task_action_send_message(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_send_message::~task_action_send_message() {}

const char* task_action_send_message::name() const { return "task_action_send_message"; }

task_action_send_message::result_type task_action_send_message::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_send_message::on_success() { return get_result(); }

int task_action_send_message::on_failed() { return get_result(); }
