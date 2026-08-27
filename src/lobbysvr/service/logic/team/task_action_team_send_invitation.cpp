// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-26 17:38:31

#include "logic/team/task_action_team_send_invitation.h"

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

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_send_invitation::task_action_team_send_invitation(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_send_invitation::~task_action_team_send_invitation() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_team_send_invitation::name() const {
  return "task_action_team_send_invitation";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_send_invitation::result_type
task_action_team_send_invitation::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  const auto& invitee = req_body.user_key();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  user_team::ptr_t team_ptr;

  if (req_body.team_key().team_id() != 0) {
    team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(req_body.team_key());
    if (!team_ptr) {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    if (!team_ptr->check_permission(team_ptr->get_configure().invite_role())) {
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
  }

  atfw::team::DTeamKey team_key;
  do {
    if (team_ptr) {
      protobuf_copy_message(team_key, team_ptr->get_team_key());
      break;
    }

    // 先尝试使用已存在的队伍
    team_ptr = user_inst->get_user_team_manager().get_team_by_team_type(atfw::shared::EN_TEAM_TYPE_NORMAL);
    if (team_ptr) {
      protobuf_copy_message(team_key, team_ptr->get_team_key());
      break;
    }

    // 如果没有队伍，先创建一个(创建者即队长，初始 shared_data 由 user_team_manager 填充)
    int32_t create_ret = RPC_AWAIT_CODE_RESULT(user_inst->get_user_team_manager().create_team(
        get_shared_context(), PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL, team_key));
    if (0 != create_ret) {
      set_response_code(create_ret);
      TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }
  } while (false);

  // 发送邀请(SS消息打包在 user_team_manager 中，业务结果透传)
  set_response_code(RPC_AWAIT_CODE_RESULT(user_inst->get_user_team_manager().send_invitation(
      get_shared_context(), team_key, invitee, req_body.team_source_type(), req_body.team_source_data())));

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_send_invitation::on_success() { return get_result(); }

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_send_invitation::on_failed() { return get_result(); }
