// Copyright 2026 atframework

#include "logic/matching/task_action_matching_check.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <data/user.h>
#include <logic/matching/user_matching_manager.h>

task_action_matching_check::task_action_matching_check(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_matching_check::~task_action_matching_check() = default;
const char* task_action_matching_check::name() const { return "task_action_matching_check"; }

task_action_matching_check::result_type task_action_matching_check::operator()() {
  auto user_inst = get_user<user>();
  if (!user_inst) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  set_response_code(RPC_AWAIT_CODE_RESULT(
      user_inst->get_user_matching_manager().check_matching(get_shared_context(), get_response_body())));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_matching_check::on_success() { return get_result(); }
int task_action_matching_check::on_failed() { return get_result(); }
