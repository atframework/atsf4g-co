// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-26 17:38:31

#include "logic/team/task_action_team_update_member_role.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/team/team_room_client_api.h>

#include <data/user.h>

#include <utility>

#include "logic/team/user_team_manager.h"

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_update_member_role::task_action_team_update_member_role(
    dispatcher_start_data_type&& param)
    : base_type(std::move(param)) {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API
task_action_team_update_member_role::~task_action_team_update_member_role() {}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API const char* task_action_team_update_member_role::name() const {
  return "task_action_team_update_member_role";
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API task_action_team_update_member_role::result_type
task_action_team_update_member_role::operator()() {
  const rpc_request_type& req_body = get_request_body();
  // rpc_response_type& rsp_body = get_response_body();

  user::ptr_t user_inst = get_user<user>();
  if (!user_inst) {
    FCTXLOGERROR(get_shared_context(), "not logined.");
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_NOT_LOGINED);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto team_ptr = user_inst->get_user_team_manager().get_team_by_team_key(req_body.team_key());
  if (!team_ptr) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 需要有设置成员角色的权限(默认 ADMIN)，权威校验由 teamsvr-room 兜底
  if (!team_ptr->check_permission(team_ptr->get_configure().set_member_role_role())) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 不能授予高于操作者自身的角色
  if (req_body.role() > team_ptr->get_cached_permission_role()) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 转发 member_set_role 到 teamsvr-room(按队伍一致性哈希路由)，业务结果经 client_result 透传
  rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageReq> ss_req{get_shared_context()};
  rpc::context::message_holder<atfw::team::SSTeamRoomSendMessageRsp> ss_rsp{get_shared_context()};
  protobuf_copy_message(*ss_req->mutable_team_key(), req_body.team_key());
  ss_req->mutable_sender_user_key()->set_zone_id(user_inst->get_zone_id());
  ss_req->mutable_sender_user_key()->set_user_id(user_inst->get_user_id());
  auto* set_role_action = ss_req->mutable_action()->mutable_member_set_role();
  protobuf_copy_message(*set_role_action->mutable_user_key(), req_body.user_key());
  set_role_action->set_role(req_body.role());

  int32_t response_code =
      RPC_AWAIT_CODE_RESULT(rpc::team::team_api::send_message(get_shared_context(), *ss_req, *ss_rsp));
  if (0 == response_code) {
    response_code = ss_rsp->client_result();
  }
  set_response_code(response_code);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_update_member_role::on_success() {
  return get_result();
}

ATFRAMEWORK_SHARED_LOBBYSVRCLIENTSERVICE_API int task_action_team_update_member_role::on_failed() {
  return get_result();
}
