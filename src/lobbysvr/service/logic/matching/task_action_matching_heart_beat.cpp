// Copyright 2026 atframework
// @brief Created by jijunliang with mako-generator.py at 2026-09-24 15:34:18

#include "logic/matching/task_action_matching_heart_beat.h"

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

#include <rpc/rpc_context.h>

#include <data/user.h>
#include <logic/matching/user_matching_manager.h>

#include <utility>

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_matching_heart_beat::task_action_matching_heart_beat(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_matching_heart_beat::~task_action_matching_heart_beat() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_matching_heart_beat::name() const {
  return "task_action_matching_heart_beat";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_matching_heart_beat::result_type
task_action_matching_heart_beat::operator()() {
  // const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user_inst->get_user_matching_manager().try_send_heartbeat(get_shared_context());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_matching_heart_beat::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_matching_heart_beat::on_failed() { return get_result(); }
