// Copyright 2021 atframework
// @brief Created by owent with generate-for-pb.py at 2021-11-14 20:57:03

#include "task_action_login.h"

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

task_action_login::task_action_login(dispatcher_start_data_t&& param) : base_type(COPP_MACRO_STD_MOVE(param)) {}
task_action_login::~task_action_login() {}

const char* task_action_login::name() const { return "task_action_login"; }

int task_action_login::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();

  // TODO ...

  return hello::err::EN_SUCCESS;
}

int task_action_login::on_success() { return get_result(); }

int task_action_login::on_failed() { return get_result(); }
