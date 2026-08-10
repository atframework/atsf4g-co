// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-05-12 11:45:33

#include "task_action_echo.h"

#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/com.orbit.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

LOBBY_SERVICE_API task_action_echo::task_action_echo(dispatcher_start_data_type &&param)
    : base_type(std::move(param)) {}

LOBBY_SERVICE_API task_action_echo::~task_action_echo() {}

LOBBY_SERVICE_API const char *task_action_echo::name() const { return "task_action_echo"; }

LOBBY_SERVICE_API task_action_echo::result_type task_action_echo::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type &req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type &rsp_body = get_response_body();

  rsp_body.set_text(req_body.text());
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

LOBBY_SERVICE_API int task_action_echo::on_success() { return get_result(); }

LOBBY_SERVICE_API int task_action_echo::on_failed() { return get_result(); }