// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-07 17:00:03

#include "logic/action/task_action_init_user.h"

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

task_action_init_user::task_action_init_user(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

task_action_init_user::~task_action_init_user() {}

const char* task_action_init_user::name() const { return "task_action_init_user"; }

task_action_init_user::result_type task_action_init_user::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  rsp_body.set_result_code(orbit_room_manager::me()->init_user(get_shared_context(), req_body));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_init_user::on_success() { return get_result(); }

int task_action_init_user::on_failed() { return get_result(); }
