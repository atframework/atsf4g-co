// Copyright 2026 atframework
// @brief Created by owent

#pragma once

#include <config/compile_optimize.h>

#include <std/explicit_declare.h>

#include <rpc/rpc_common_types.h>

#ifndef TEAM_SDK_ROOM_API
#  define TEAM_SDK_ROOM_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

namespace atframework {
namespace team {
class SSTeamRoomCreateReq;
class SSTeamRoomCreateRsp;
class SSTeamRoomSendMessageReq;
class SSTeamRoomSendMessageRsp;
class SSTeamRoomHeartbeatReq;
class SSTeamRoomHeartbeatRsp;
class SSTeamRoomAddInvitationReq;
class SSTeamRoomAddInvitationRsp;
class SSTeamRoomApproveInvitationReq;
class SSTeamRoomApproveInvitationRsp;
class SSTeamRoomRejectInvitationReq;
class SSTeamRoomRejectInvitationRsp;
class SSTeamRoomAddJoinRequestReq;
class SSTeamRoomAddJoinRequestRsp;
class SSTeamRoomApproveJoinRequestReq;
class SSTeamRoomApproveJoinRequestRsp;
class SSTeamRoomRejectJoinRequestReq;
class SSTeamRoomRejectJoinRequestRsp;
}  // namespace team
}  // namespace atframework

namespace rpc {
class context;

namespace team {
namespace team_api {

/**
 * @brief 创建队伍，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 team_key 提取 team_id，从 sender_user_key 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type create(
    rpc::context& ctx, atfw::team::SSTeamRoomCreateReq& req, atfw::team::SSTeamRoomCreateRsp& rsp,
    bool no_wait = false);

/**
 * @brief 发送队伍消息(队伍操作)，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 team_key 提取 team_id，从 sender_user_key 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type send_message(
    rpc::context& ctx, atfw::team::SSTeamRoomSendMessageReq& req, atfw::team::SSTeamRoomSendMessageRsp& rsp,
    bool no_wait = false);

/**
 * @brief 成员心跳，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 team_key 提取 team_id，从 user_key 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type heartbeat(
    rpc::context& ctx, atfw::team::SSTeamRoomHeartbeatReq& req, atfw::team::SSTeamRoomHeartbeatRsp& rsp,
    bool no_wait = false);

/**
 * @brief 添加邀请，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 invitation.team_key 提取 team_id，从 invitation.inviter 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type add_invitation(
    rpc::context& ctx, atfw::team::SSTeamRoomAddInvitationReq& req, atfw::team::SSTeamRoomAddInvitationRsp& rsp,
    bool no_wait = false);

/**
 * @brief 批准邀请，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 team_key 提取 team_id，从 invitee 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type approve_invitation(
    rpc::context& ctx, atfw::team::SSTeamRoomApproveInvitationReq& req, atfw::team::SSTeamRoomApproveInvitationRsp& rsp,
    bool no_wait = false);

/**
 * @brief 拒绝邀请，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 team_key 提取 team_id，从 invitee 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type reject_invitation(
    rpc::context& ctx, atfw::team::SSTeamRoomRejectInvitationReq& req, atfw::team::SSTeamRoomRejectInvitationRsp& rsp,
    bool no_wait = false);

/**
 * @brief 添加加入请求，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 join_request.team_key 提取 team_id，从 join_request.requester 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type add_join_request(
    rpc::context& ctx, atfw::team::SSTeamRoomAddJoinRequestReq& req, atfw::team::SSTeamRoomAddJoinRequestRsp& rsp,
    bool no_wait = false);

/**
 * @brief 批准加入请求，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 team_key 提取 team_id，从 applicant 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type approve_join_request(
    rpc::context& ctx, atfw::team::SSTeamRoomApproveJoinRequestReq& req,
    atfw::team::SSTeamRoomApproveJoinRequestRsp& rsp, bool no_wait = false);

/**
 * @brief 拒绝加入请求，目标 teamsvr-room 节点选择内嵌在本接口内
 *
 * @param ctx RPC上下文
 * @param req 请求体(从 team_key 提取 team_id，从 applicant 提取 zone_id 做一致性哈希路由)
 * @param rsp 响应体
 * @param no_wait 是否不等待
 * @return rpc::result_code_type 发送结果
 */
ATFW_EXPLICIT_NODISCARD_ATTR TEAM_SDK_ROOM_API rpc::result_code_type reject_join_request(
    rpc::context& ctx, atfw::team::SSTeamRoomRejectJoinRequestReq& req,
    atfw::team::SSTeamRoomRejectJoinRequestRsp& rsp, bool no_wait = false);

}  // namespace team_api
}  // namespace team
}  // namespace rpc
