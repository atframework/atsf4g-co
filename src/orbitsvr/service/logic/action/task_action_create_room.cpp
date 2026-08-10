// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-04 17:26:47

#include "logic/action/task_action_create_room.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/orbit_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <logic/room/orbit_room_manager.h>

#include <utility>

ORBIT_SERVER_API task_action_create_room::task_action_create_room(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_SERVER_API task_action_create_room::~task_action_create_room() {}

ORBIT_SERVER_API const char* task_action_create_room::name() const { return "task_action_create_room"; }

ORBIT_SERVER_API task_action_create_room::result_type task_action_create_room::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  rsp_body.set_result_code(RPC_AWAIT_CODE_RESULT(
      orbit_room_manager::me()->create_room(get_shared_context(), req_body, get_request_node_id())));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_SERVER_API int task_action_create_room::on_success() { return get_result(); }

ORBIT_SERVER_API int task_action_create_room::on_failed() { return get_result(); }
