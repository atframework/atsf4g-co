// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-09-28 17:11:31

#include "logic/friend_api/task_action_friend_invite.h"

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

#include <utility>

#include "data/user.h"
#include "logic/friend_api/user_friend_api_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_invite::task_action_friend_invite(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_invite::~task_action_friend_invite() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_friend_invite::name() const {
  return "task_action_friend_invite";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_friend_invite::result_type
task_action_friend_invite::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // TODO(any): 短账号ID转换功能，等接入该功能后再集成。目前先不支持
  // if ((0 == req_body.user_key().user_id() || 0 == req_body.user_key().zone_id()) && 0 != req_body.account_id()) {
  //   set_response_code(RPC_AWAIT_CODE_RESULT(
  //       rpc::db::user::get_by_account_id(get_shared_context(), req_body.account_id(), user_id, zone_id)));
  //   if (get_response_code() < 0) {
  //     TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  //   }
  // }

  if (0 == req_body.user_key().user_id() || 0 == req_body.user_key().zone_id()) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user_inst->get_user_friend_api_manager().refresh_feature_limit_minute(get_shared_context());

  set_response_code(RPC_AWAIT_CODE_RESULT(
      user_inst->get_user_friend_api_manager().send_invite(get_shared_context(), req_body.user_key())));

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_invite::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_friend_invite::on_failed() { return get_result(); }
