// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-04 17:26:47

#include "logic/orbit/task_action_orbit_room_event_sync.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/lobby_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

LOBBY_SERVICE_API task_action_orbit_room_event_sync::task_action_orbit_room_event_sync(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

LOBBY_SERVICE_API task_action_orbit_room_event_sync::~task_action_orbit_room_event_sync() {}

LOBBY_SERVICE_API const char* task_action_orbit_room_event_sync::name() const {
  return "task_action_orbit_room_event_sync";
}

LOBBY_SERVICE_API task_action_orbit_room_event_sync::result_type task_action_orbit_room_event_sync::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

LOBBY_SERVICE_API int task_action_orbit_room_event_sync::on_success() { return get_result(); }

LOBBY_SERVICE_API int task_action_orbit_room_event_sync::on_failed() { return get_result(); }
