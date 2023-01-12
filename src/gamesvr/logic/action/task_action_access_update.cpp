// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:33:44

#include "logic/action/task_action_access_update.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <data/player.h>

task_action_access_update::task_action_access_update(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_access_update::~task_action_access_update() {}

const char* task_action_access_update::name() const { return "task_action_access_update"; }

task_action_access_update::result_type task_action_access_update::operator()() {
  const rpc_request_type& req_body = get_request_body();

  player::ptr_t user = get_player<player>();
  if (!user) {
    FWLOGERROR("not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (user->get_login_info().login_code() != req_body.old_access()) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user->get_login_info().set_login_code(req_body.new_access());
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_access_update::on_success() { return get_result(); }

int task_action_access_update::on_failed() { return get_result(); }
