// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-20 16:47:38

#include "logic/action/task_action_management_gm_reset_limit.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/friend_management_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API
task_action_management_gm_reset_limit::task_action_management_gm_reset_limit(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API
    task_action_management_gm_reset_limit::~task_action_management_gm_reset_limit() {}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API const char* task_action_management_gm_reset_limit::name() const {
  return "task_action_management_gm_reset_limit";
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API task_action_management_gm_reset_limit::result_type
task_action_management_gm_reset_limit::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API int task_action_management_gm_reset_limit::on_success() {
  return get_result();
}

ATFRAMEWORK_FRIEND_API_FRIENDMANAGEMENTSERVICE_API int task_action_management_gm_reset_limit::on_failed() {
  return get_result();
}
