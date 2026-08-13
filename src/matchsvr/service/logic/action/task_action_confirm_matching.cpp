// Copyright 2026 atframework
// @brief Created by jijunliang with mako-generator.py at 2026-08-04 16:51:07

#include "logic/action/task_action_confirm_matching.h"

#include <std/explicit_declare.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/match_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <utility>

#include "logic/matching/matching_manager.h"

MATCH_SERVICE_API task_action_confirm_matching::task_action_confirm_matching(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}

MATCH_SERVICE_API task_action_confirm_matching::~task_action_confirm_matching() {}

MATCH_SERVICE_API const char *task_action_confirm_matching::name() const {
  return "task_action_confirm_matching";
}

MATCH_SERVICE_API task_action_confirm_matching::result_type task_action_confirm_matching::operator()() {
  matching_manager::me()->confirm_matching(get_shared_context(), get_request_body(), get_response_body());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

MATCH_SERVICE_API int task_action_confirm_matching::on_success() { return get_result(); }

MATCH_SERVICE_API int task_action_confirm_matching::on_failed() { return get_result(); }
