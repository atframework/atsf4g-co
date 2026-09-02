// Copyright 2026 atframework

#include "logic/action/task_action_matching_heart_bear.h"

#include <std/explicit_declare.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include "logic/matching/matching_manager.h"

task_action_matching_heart_bear::task_action_matching_heart_bear(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_matching_heart_bear::~task_action_matching_heart_bear() = default;

const char* task_action_matching_heart_bear::name() const { return "task_action_matching_heart_bear"; }

task_action_matching_heart_bear::result_type task_action_matching_heart_bear::operator()() {
  matching_manager::me()->check_matching(get_shared_context(), get_request_body(), get_response_body(),
                                         get_request_node_id());
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_matching_heart_bear::on_success() { return get_result(); }
int task_action_matching_heart_bear::on_failed() { return get_result(); }
