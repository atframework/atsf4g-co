// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:38:42

#include "task_action_login_auth.h"

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

task_action_login_auth::task_action_login_auth(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_login_auth::~task_action_login_auth() {}

const char* task_action_login_auth::name() const { return "task_action_login_auth"; }

task_action_login_auth::result_type task_action_login_auth::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_login_auth::on_success() { return get_result(); }

int task_action_login_auth::on_failed() { return get_result(); }
