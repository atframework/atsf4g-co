// Copyright 2026 atframework
// @brief Created by jijunliang with mako-generator.py at 2026-08-31 16:09:40

#include "logic/action/task_action_acknowledge_matching_events.h"

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

ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API task_action_acknowledge_matching_events::task_action_acknowledge_matching_events(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API task_action_acknowledge_matching_events::~task_action_acknowledge_matching_events() {}

ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API const char *task_action_acknowledge_matching_events::name() const {
  return "task_action_acknowledge_matching_events";
}

ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API task_action_acknowledge_matching_events::result_type task_action_acknowledge_matching_events::operator()() {
  if (is_stream_rpc()) {
    disable_response_message();
  }
  TASK_ACTION_RETURN_CODE(matching_manager::me()->acknowledge_matching_events(get_shared_context(),
                                                                              get_request_body()));
}

ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API int task_action_acknowledge_matching_events::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_MATCHSVRSERVICE_API int task_action_acknowledge_matching_events::on_failed() { return get_result(); }
