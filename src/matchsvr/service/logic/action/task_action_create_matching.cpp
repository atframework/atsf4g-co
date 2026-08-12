// Copyright 2026 atframework

#include "logic/action/task_action_create_matching.h"

#include <std/explicit_declare.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include "logic/matching/matching_manager.h"

task_action_create_matching::task_action_create_matching(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_create_matching::~task_action_create_matching() = default;

const char* task_action_create_matching::name() const { return "task_action_create_matching"; }

task_action_create_matching::result_type task_action_create_matching::operator()() {
  matching_manager::me()->create_matching(get_shared_context(), get_request_body(), get_response_body());
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_create_matching::on_success() { return get_result(); }
int task_action_create_matching::on_failed() { return get_result(); }
