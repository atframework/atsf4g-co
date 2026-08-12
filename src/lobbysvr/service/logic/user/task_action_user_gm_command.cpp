// Copyright 2026 atframework
// @brief Created by yousongyang with mako-generator.py at 2026-08-12 14:10:41

#include "logic/user/task_action_user_gm_command.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <data/user.h>

#include <utility>

GAMECLIENT_SERVICE_API task_action_user_gm_command::task_action_user_gm_command(dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

GAMECLIENT_SERVICE_API task_action_user_gm_command::~task_action_user_gm_command() {}

GAMECLIENT_SERVICE_API const char* task_action_user_gm_command::name() const { return "task_action_user_gm_command"; }

GAMECLIENT_SERVICE_API task_action_user_gm_command::result_type task_action_user_gm_command::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // TODO ...

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

GAMECLIENT_SERVICE_API int task_action_user_gm_command::on_success() { return get_result(); }

GAMECLIENT_SERVICE_API int task_action_user_gm_command::on_failed() { return get_result(); }
