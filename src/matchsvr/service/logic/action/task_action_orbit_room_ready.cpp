// Copyright 2026 atframework

#include "logic/action/task_action_orbit_room_ready.h"

#include "logic/matching/matching_manager.h"

task_action_orbit_room_ready::task_action_orbit_room_ready(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_orbit_room_ready::~task_action_orbit_room_ready() = default;

const char* task_action_orbit_room_ready::name() const { return "task_action_orbit_room_ready"; }

task_action_orbit_room_ready::result_type task_action_orbit_room_ready::operator()() {
  RPC_AWAIT_IGNORE_RESULT(matching_manager::me()->orbit_room_ready(get_shared_context(), get_request_body(),
                                                                   get_response_body(), get_request_node_id()));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_orbit_room_ready::on_success() { return get_result(); }

int task_action_orbit_room_ready::on_failed() { return get_result(); }
