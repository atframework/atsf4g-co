// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-20 19:15:12

#include "logic/action/task_action_reject_invitation.h"

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

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API task_action_reject_invitation::task_action_reject_invitation(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API task_action_reject_invitation::~task_action_reject_invitation() {}

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API const char* task_action_reject_invitation::name() const {
  return "task_action_reject_invitation";
}

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API task_action_reject_invitation::result_type
task_action_reject_invitation::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API int task_action_reject_invitation::on_success() { return get_result(); }

ATFRAMEWORK_TEAM_TEAMROOMSERVICE_API int task_action_reject_invitation::on_failed() { return get_result(); }
