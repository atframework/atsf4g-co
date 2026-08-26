// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-26 17:38:31

#include "logic/team/task_action_team_reject_invitation.h"

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

#include <utility>

#include "logic/team/user_team_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_reject_invitation::task_action_team_reject_invitation(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_reject_invitation::~task_action_team_reject_invitation() {
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_team_reject_invitation::name() const {
  return "task_action_team_reject_invitation";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_reject_invitation::result_type
task_action_team_reject_invitation::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto invitation_ptr = user_inst->get_user_team_manager().get_pending_invitation(req_body.team_key());
  if (!invitation_ptr) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  if (get_shared_context().logical_now() >= protobuf_to_system_clock(invitation_ptr->expired_timepoint())) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 转发 reject_invitation 到 teamsvr-room(按队伍一致性哈希路由)，SS消息打包在 user_team_manager 中，业务结果透传
  set_response_code(RPC_AWAIT_CODE_RESULT(
      user_inst->get_user_team_manager().reject_invitation(get_shared_context(), invitation_ptr)));

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_reject_invitation::on_success() {
  return get_result();
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_reject_invitation::on_failed() {
  return get_result();
}
