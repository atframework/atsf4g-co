// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-11-02 14:32:30

#include "logic/orbit_action/task_action_user_end_join.h"

#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/com.orbit.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

#include <logic/room/orbit_room_manager.h>

#include <string>
#include <utility>

ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API task_action_user_end_join::task_action_user_end_join(
    dispatcher_start_data_type &&param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API task_action_user_end_join::~task_action_user_end_join() {}

ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API const char *task_action_user_end_join::name() const {
  return "task_action_user_end_join";
}

ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API task_action_user_end_join::result_type
task_action_user_end_join::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type &req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type &rsp_body = get_response_body();

  const std::string &client_id = get_request_client_id();
  RPC_AWAIT_CODE_RESULT(orbit_room_manager::me()->on_room_end_join(get_shared_context(), client_id));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API int task_action_user_end_join::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_ORBITSERVERRPCSERVICE_API int task_action_user_end_join::on_failed() { return get_result(); }
