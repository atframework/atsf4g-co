// Copyright 2026 atframework
// @brief Created by owent with mako-generator.py at 2026-08-10 20:11:30

#include "logic/action/task_action_send_message.h"

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
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/team/team_common_api.h>

#include <utility>

#include "logic/room/team_room_manager.h"

task_action_send_message::task_action_send_message(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}

task_action_send_message::~task_action_send_message() {}

const char* task_action_send_message::name() const { return "task_action_send_message"; }

task_action_send_message::result_type task_action_send_message::operator()() {
  rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  if (!req_body.has_action()) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 按队伍一致性哈希路由到 teamsvr-room 节点，不在本节点则转发
  uint64_t dest_server_id = rpc::team::team_api::get_teamsvr_room_server_id_of_zone(req_body.team_key());
  if (0 == dest_server_id) {
    FCTXLOGERROR(get_shared_context(), "no ready teamsvr-room node for team {}:{}", req_body.team_key().zone_id(), req_body.team_key().team_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }
  if (dest_server_id != logic_config::me()->get_local_server_id()) {
    bool forward_ok = false;
    auto forward_ret = RPC_AWAIT_CODE_RESULT(forward_rpc(dest_server_id, false, forward_ok));
    if (0 != forward_ret || !forward_ok) {
      FCTXLOGERROR(get_shared_context(), "forward team {}:{} message to dest server {} failed! ret:{} ok:{}",
                   req_body.team_key().zone_id(), req_body.team_key().team_id(), dest_server_id, forward_ret, forward_ok ? 1 : 0);
      set_response_code(0 != forward_ret ? forward_ret : PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  // 本节点处理: 查找或创建房间(订阅频道并接管乐观锁)
  auto room = team_room_manager::me()->mutable_room(get_shared_context(), req_body.team_key());
  if (!room) {
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  auto ret = RPC_AWAIT_CODE_RESULT(room->await_ready(get_shared_context()));
  if (0 == ret) {
    // 权限校验: 没权限直接返回错误，不提交 DTeamAction 到房间频道
    ret = RPC_AWAIT_CODE_RESULT(room->check_action_permission(req_body.sender_user_key(), req_body.action()));
  }
  if (0 == ret) {
    // Admission 动作必须走专用流程，不能直接写原始 DTeamAction：
    // approve 流程还需要补充成员事件，add 流程需要规范化过期时间和个人频道数据。
    switch (req_body.action().action_case()) {
      case atfw::team::DTeamAction::kAddInvitation: {
        auto translated_req = rpc::make_shared_message<atfw::team::SSTeamRoomAddInvitationReq>(get_shared_context());
        protobuf_move_message(*translated_req->mutable_invitation(),
                              std::move(*req_body.mutable_action()->mutable_add_invitation()));
        protobuf_copy_message(*translated_req->mutable_sender_user_key(), req_body.sender_user_key());
        ret = RPC_AWAIT_CODE_RESULT(room->add_invitation(get_shared_context(), *translated_req));
        break;
      }
      case atfw::team::DTeamAction::kApproveInvitation: {
        auto translated_req =
            rpc::make_shared_message<atfw::team::SSTeamRoomApproveInvitationReq>(get_shared_context());
        room->dump_team_key(*translated_req->mutable_team_key());
        protobuf_copy_message(*translated_req->mutable_sender_user_key(), req_body.sender_user_key());
        auto* approve_invitation = req_body.mutable_action()->mutable_approve_invitation();
        protobuf_move_message(*translated_req->mutable_invitee(), std::move(*approve_invitation->mutable_invitee()));
        for (auto& member_admission : *approve_invitation->mutable_member_admission_data()) {
          if (member_admission.user_key().zone_id() != req_body.sender_user_key().zone_id() ||
              member_admission.user_key().user_id() != req_body.sender_user_key().user_id()) {
            continue;
          }
          protobuf_move_message(*translated_req->mutable_shared_member_data(),
                                std::move(*member_admission.mutable_member_admission_data()));
          break;
        }
        ret = RPC_AWAIT_CODE_RESULT(room->approve_invitation(get_shared_context(), *translated_req));
        break;
      }
      case atfw::team::DTeamAction::kRejectInvitation: {
        auto translated_req = rpc::make_shared_message<atfw::team::SSTeamRoomRejectInvitationReq>(get_shared_context());
        room->dump_team_key(*translated_req->mutable_team_key());
        protobuf_copy_message(*translated_req->mutable_sender_user_key(), req_body.sender_user_key());
        protobuf_move_message(*translated_req->mutable_invitee(),
                              std::move(*req_body.mutable_action()->mutable_reject_invitation()->mutable_invitee()));
        ret = RPC_AWAIT_CODE_RESULT(room->reject_invitation(get_shared_context(), *translated_req));
        break;
      }
      case atfw::team::DTeamAction::kAddJoinRequest: {
        auto translated_req = rpc::make_shared_message<atfw::team::SSTeamRoomAddJoinRequestReq>(get_shared_context());
        protobuf_copy_message(*translated_req->mutable_sender_user_key(), req_body.sender_user_key());
        protobuf_move_message(*translated_req->mutable_join_request(),
                              std::move(*req_body.mutable_action()->mutable_add_join_request()));
        ret = RPC_AWAIT_CODE_RESULT(room->add_join_request(get_shared_context(), *translated_req));
        break;
      }
      case atfw::team::DTeamAction::kApproveJoinRequest: {
        auto translated_req =
            rpc::make_shared_message<atfw::team::SSTeamRoomApproveJoinRequestReq>(get_shared_context());
        room->dump_team_key(*translated_req->mutable_team_key());
        protobuf_copy_message(*translated_req->mutable_sender_user_key(), req_body.sender_user_key());
        protobuf_move_message(
            *translated_req->mutable_applicant(),
            std::move(*req_body.mutable_action()->mutable_approve_join_request()->mutable_requester()));
        ret = RPC_AWAIT_CODE_RESULT(room->approve_join_request(get_shared_context(), *translated_req));
        break;
      }
      case atfw::team::DTeamAction::kRejectJoinRequest: {
        auto translated_req =
            rpc::make_shared_message<atfw::team::SSTeamRoomRejectJoinRequestReq>(get_shared_context());
        room->dump_team_key(*translated_req->mutable_team_key());
        protobuf_copy_message(*translated_req->mutable_sender_user_key(), req_body.sender_user_key());
        protobuf_move_message(
            *translated_req->mutable_applicant(),
            std::move(*req_body.mutable_action()->mutable_reject_join_request()->mutable_requester()));
        ret = RPC_AWAIT_CODE_RESULT(room->reject_join_request(get_shared_context(), *translated_req));
        break;
      }
      default:
        // GAP-05: 管理员/队长可更新他人成员数据，但请求中携带的他人 user_router_server_id 不可信
        // (成员切换节点后会自己心跳上报)，action 层在写入前清零，apply 层只接受非零值
        if (atfw::team::DTeamAction::kMemberUpdate == req_body.action().action_case() &&
            req_body.action().member_update().user_router_server_id() != 0 &&
            (req_body.action().member_update().user_key().zone_id() != req_body.sender_user_key().zone_id() ||
             req_body.action().member_update().user_key().user_id() != req_body.sender_user_key().user_id())) {
          req_body.mutable_action()->mutable_member_update()->set_user_router_server_id(0);
        }
        ret = RPC_AWAIT_CODE_RESULT(room->send_action(get_shared_context(), req_body.action()));
        break;
    }
  }

  rsp_body.set_client_result(ret);
  if (ret < 0) {
    set_response_code(ret);
  } else if (req_body.sender_user_key().zone_id() != 0 && req_body.sender_user_key().user_id() != 0) {
    // 激活一下发请求者,放到LRU map的末尾
    room->find_member(req_body.sender_user_key(), true);
  }

  RPC_AWAIT_IGNORE_RESULT(room->flush_pending_channel_message(get_shared_context()));
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_send_message::on_success() { return get_result(); }

int task_action_send_message::on_failed() { return get_result(); }
