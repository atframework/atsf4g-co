// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-04-29 11:47:42

#include "task_action_forward_to_server.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/server_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

ORBIT_MASTER_SERVICE_API task_action_forward_to_server::task_action_forward_to_server(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ORBIT_MASTER_SERVICE_API task_action_forward_to_server::~task_action_forward_to_server() {}

ORBIT_MASTER_SERVICE_API const char* task_action_forward_to_server::name() const {
  return "task_action_forward_to_server";
}

ORBIT_MASTER_SERVICE_API task_action_forward_to_server::result_type task_action_forward_to_server::operator()() {
  ATFW_EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ORBIT_MASTER_SERVICE_API int task_action_forward_to_server::on_success() { return get_result(); }

ORBIT_MASTER_SERVICE_API int task_action_forward_to_server::on_failed() { return get_result(); }
