// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-04 19:28:41

#include "task_action_user_finish.h"

#include <std/explicit_declare.h>

#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/com.orbit.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

#include <logic/room/orbit_room_manager.h>

GAME_SERVICE_API task_action_user_finish::task_action_user_finish(dispatcher_start_data_type &&param)
    : base_type(std::move(param)) {}

GAME_SERVICE_API task_action_user_finish::~task_action_user_finish() {}

GAME_SERVICE_API const char *task_action_user_finish::name() const { return "task_action_user_finish"; }

GAME_SERVICE_API task_action_user_finish::result_type task_action_user_finish::operator()() {
  const rpc_request_type &req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type &rsp_body = get_response_body();

  const std::string &client_id = get_request_client_id();
  RPC_AWAIT_CODE_RESULT(orbit_room_manager::me()->on_user_finish(get_shared_context(), client_id, req_body.results()));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAME_SERVICE_API int task_action_user_finish::on_success() { return get_result(); }

GAME_SERVICE_API int task_action_user_finish::on_failed() { return get_result(); }