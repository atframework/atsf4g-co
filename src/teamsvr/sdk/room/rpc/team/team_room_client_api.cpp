// Copyright 2026 atframework
// @brief Created by owent

#include "rpc/team/team_room_client_api.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/team_room_service.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/rpc_context.h>
#include <rpc/team/team_common_api.h>
#include <rpc/team/teamroomservice.atfw.gen.h>

namespace rpc {
namespace team {
namespace team_api {

namespace {
// 按 (zone_id, team_id) 一致性哈希选择 teamsvr-room 节点并调用 RPC，无可用节点时返回
// EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE。CALL 为生成的 rpc::team::<method>(ctx, server_id, req, rsp, no_wait)
template <class REQ, class RSP, class CALL>
static rpc::result_code_type internal_call_team_room(rpc::context& ctx, uint32_t zone_id, int64_t team_id, REQ& req,
                                                     RSP& rsp, bool no_wait, CALL&& call) {
  if (0 == team_id || 0 == zone_id) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  uint64_t dest_server_id = get_teamsvr_room_server_id_of_zone(zone_id, team_id);
  if (0 == dest_server_id) {
    FCTXLOGDEBUG(ctx, "No teamsvr-room server available for team:({}) zone:({})", team_id, zone_id);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }

  auto ret = RPC_AWAIT_CODE_RESULT(call(ctx, dest_server_id, req, rsp, no_wait));
  RPC_RETURN_CODE(ret);
}
}  // namespace

TEAM_SDK_ROOM_API rpc::result_code_type create(rpc::context& ctx, atfw::team::SSTeamRoomCreateReq& req,
                                               atfw::team::SSTeamRoomCreateRsp& rsp, bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.sender_user_key().zone_id(), req.team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomCreateReq& inner_req,
         atfw::team::SSTeamRoomCreateRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::create(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type send_message(rpc::context& ctx, atfw::team::SSTeamRoomSendMessageReq& req,
                                                     atfw::team::SSTeamRoomSendMessageRsp& rsp, bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.sender_user_key().zone_id(), req.team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomSendMessageReq& inner_req,
         atfw::team::SSTeamRoomSendMessageRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::send_message(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type heartbeat(rpc::context& ctx, atfw::team::SSTeamRoomHeartbeatReq& req,
                                                  atfw::team::SSTeamRoomHeartbeatRsp& rsp, bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.user_key().zone_id(), req.team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomHeartbeatReq& inner_req,
         atfw::team::SSTeamRoomHeartbeatRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::heartbeat(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type add_invitation(rpc::context& ctx, atfw::team::SSTeamRoomAddInvitationReq& req,
                                                       atfw::team::SSTeamRoomAddInvitationRsp& rsp, bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.invitation().inviter().zone_id(), req.invitation().team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomAddInvitationReq& inner_req,
         atfw::team::SSTeamRoomAddInvitationRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::add_invitation(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type approve_invitation(
    rpc::context& ctx, atfw::team::SSTeamRoomApproveInvitationReq& req,
    atfw::team::SSTeamRoomApproveInvitationRsp& rsp, bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.invitee().zone_id(), req.team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomApproveInvitationReq& inner_req,
         atfw::team::SSTeamRoomApproveInvitationRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::approve_invitation(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type reject_invitation(rpc::context& ctx,
                                                          atfw::team::SSTeamRoomRejectInvitationReq& req,
                                                          atfw::team::SSTeamRoomRejectInvitationRsp& rsp,
                                                          bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.invitee().zone_id(), req.team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomRejectInvitationReq& inner_req,
         atfw::team::SSTeamRoomRejectInvitationRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::reject_invitation(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type add_join_request(rpc::context& ctx,
                                                         atfw::team::SSTeamRoomAddJoinRequestReq& req,
                                                         atfw::team::SSTeamRoomAddJoinRequestRsp& rsp,
                                                         bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.join_request().requester().zone_id(), req.join_request().team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomAddJoinRequestReq& inner_req,
         atfw::team::SSTeamRoomAddJoinRequestRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::add_join_request(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type approve_join_request(
    rpc::context& ctx, atfw::team::SSTeamRoomApproveJoinRequestReq& req,
    atfw::team::SSTeamRoomApproveJoinRequestRsp& rsp, bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.applicant().zone_id(), req.team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomApproveJoinRequestReq& inner_req,
         atfw::team::SSTeamRoomApproveJoinRequestRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::approve_join_request(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

TEAM_SDK_ROOM_API rpc::result_code_type reject_join_request(
    rpc::context& ctx, atfw::team::SSTeamRoomRejectJoinRequestReq& req,
    atfw::team::SSTeamRoomRejectJoinRequestRsp& rsp, bool no_wait) {
  auto ret = RPC_AWAIT_CODE_RESULT(internal_call_team_room(
      ctx, req.applicant().zone_id(), req.team_key().team_id(), req, rsp, no_wait,
      [](rpc::context& inner_ctx, uint64_t dest_server_id, atfw::team::SSTeamRoomRejectJoinRequestReq& inner_req,
         atfw::team::SSTeamRoomRejectJoinRequestRsp& inner_rsp,
         bool inner_no_wait) -> rpc::result_code_type {
        RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
            rpc::team::reject_join_request(inner_ctx, dest_server_id, inner_req, inner_rsp, inner_no_wait)));
      }));
  RPC_RETURN_CODE(ret);
}

}  // namespace team_api
}  // namespace team
}  // namespace rpc
