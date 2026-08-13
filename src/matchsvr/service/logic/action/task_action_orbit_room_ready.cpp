// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-13 13:22:55

#include "logic/action/task_action_orbit_room_ready.h"

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

#include "logic/matching/matching_manager.h"

MATCH_SERVICE_API task_action_orbit_room_ready::task_action_orbit_room_ready(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

MATCH_SERVICE_API task_action_orbit_room_ready::~task_action_orbit_room_ready() {}

MATCH_SERVICE_API const char* task_action_orbit_room_ready::name() const { return "task_action_orbit_room_ready"; }

MATCH_SERVICE_API task_action_orbit_room_ready::result_type task_action_orbit_room_ready::operator()() {
  RPC_AWAIT_IGNORE_RESULT(matching_manager::me()->orbit_room_ready(get_shared_context(), get_request_body(),
                                                                   get_response_body(), get_request_node_id()));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

MATCH_SERVICE_API int task_action_orbit_room_ready::on_success() { return get_result(); }

MATCH_SERVICE_API int task_action_orbit_room_ready::on_failed() { return get_result(); }
