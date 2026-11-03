// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-11-02 14:33:35

#include "logic/action/task_action_orbit_room_end_join.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <rpc/orbit/orbitsvrmatchsvrservice.atfw.gen.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

ATFRAMEWORK_SHARED_ORBITSVRMATCHSVRSERVICE_API task_action_orbit_room_end_join::task_action_orbit_room_end_join(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_ORBITSVRMATCHSVRSERVICE_API task_action_orbit_room_end_join::~task_action_orbit_room_end_join() {}

ATFRAMEWORK_SHARED_ORBITSVRMATCHSVRSERVICE_API const char* task_action_orbit_room_end_join::name() const {
  return "task_action_orbit_room_end_join";
}

ATFRAMEWORK_SHARED_ORBITSVRMATCHSVRSERVICE_API task_action_orbit_room_end_join::result_type
task_action_orbit_room_end_join::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_ORBITSVRMATCHSVRSERVICE_API int task_action_orbit_room_end_join::on_success() {
  return get_result();
}

ATFRAMEWORK_SHARED_ORBITSVRMATCHSVRSERVICE_API int task_action_orbit_room_end_join::on_failed() { return get_result(); }
