package atsf4g_go_robot_rpc

import (
	"fmt"

	pu "github.com/atframework/atframe-utils-go/proto_utility"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	base "github.com/atframework/robot-go/base"
	user_data "github.com/atframework/robot-go/data"
)

// TeamViewData 是当前用户缓存在本地的队伍视图:
// 由 SCUserDirtyChgSync 的 dirty_team 推送或 user_get_info(need_user_team) 维护，用于自测时核对服务端下发内容。
type TeamViewData struct {
	Snapshot *public_protocol_pbdesc.DUserTeamSnapshot // 最近一次完整快照
	// 之后收到的增量动作(仅保留最近若干条，便于回放检查)
	LastActions  []*public_protocol_pbdesc.DTeamAction
	DirtySyncSeq int64 // 收到过的 dirty_team 推送次数(快照+增量)
}

const teamViewMaxKeepActions = 32

// SaveTeamSnapshot 覆盖式保存队伍快照视图。
func SaveTeamSnapshot(user user_data.User, snapshot *public_protocol_pbdesc.DUserTeamSnapshot) {
	if snapshot == nil {
		return
	}
	view := GetTeamView(user)
	view.Snapshot = snapshot
	view.DirtySyncSeq++
	user.SetExtralData("TeamView", view)
}

// AppendTeamIncreaseActions 追加一批增量动作到队伍视图。
func AppendTeamIncreaseActions(user user_data.User, actions []*public_protocol_pbdesc.DTeamAction) {
	if len(actions) == 0 {
		return
	}
	view := GetTeamView(user)
	view.LastActions = append(view.LastActions, actions...)
	if len(view.LastActions) > teamViewMaxKeepActions {
		view.LastActions = view.LastActions[len(view.LastActions)-teamViewMaxKeepActions:]
	}
	view.DirtySyncSeq++
	user.SetExtralData("TeamView", view)
}

// GetTeamView 取出当前缓存的队伍视图(不存在则返回空视图)。
func GetTeamView(user user_data.User) *TeamViewData {
	view, ok := user.GetExtralData("TeamView").(*TeamViewData)
	if !ok || view == nil {
		return &TeamViewData{}
	}
	return view
}

// ClearTeamView 清空本地队伍视图(退出队伍/被移出队伍时使用)。
func ClearTeamView(user user_data.User) {
	user.SetExtralData("TeamView", &TeamViewData{})
}

// BuildTeamKey 构造队伍 Key。zoneId 传 0 时使用当前用户所在区服。
func BuildTeamKey(user user_data.User, teamId int64, zoneId uint32) *public_protocol_pbdesc.DTeamKey {
	if teamId == 0 {
		return nil
	}
	if zoneId == 0 {
		zoneId = user.GetZoneId()
	}
	return &public_protocol_pbdesc.DTeamKey{
		TeamId: teamId,
		ZoneId: zoneId,
	}
}

// BuildUserKey 构造用户 Key。zoneId 传 0 时使用当前用户所在区服。
func BuildUserKey(user user_data.User, userId uint64, zoneId uint32) *public_protocol_pbdesc.DUserIDKey {
	if zoneId == 0 {
		zoneId = user.GetZoneId()
	}
	return &public_protocol_pbdesc.DUserIDKey{
		UserId: userId,
		ZoneId: zoneId,
	}
}

// BuildTeamMatchingData 构造队伍共享数据: 战斗模块的 matching 开关。
func BuildTeamMatchingData(matching bool) *public_protocol_pbdesc.DTeamSharedDataModule {
	return &public_protocol_pbdesc.DTeamSharedDataModule{
		ModuleType: &public_protocol_pbdesc.DTeamSharedDataModule_Battle{
			Battle: &public_protocol_pbdesc.DTeamSharedDataTypeBattle{
				DataType: &public_protocol_pbdesc.DTeamSharedDataTypeBattle_Matching{
					Matching: matching,
				},
			},
		},
	}
}

// BuildTeamMemberReadyData 构造成员共享数据: 战斗模块的 ready 开关。
func BuildTeamMemberReadyData(ready bool) *public_protocol_pbdesc.DTeamMemberSharedDataModule {
	return &public_protocol_pbdesc.DTeamMemberSharedDataModule{
		ModuleType: &public_protocol_pbdesc.DTeamMemberSharedDataModule_Battle{
			Battle: &public_protocol_pbdesc.DTeamMemberSharedDataTypeBattle{
				DataType: &public_protocol_pbdesc.DTeamMemberSharedDataTypeBattle_Ready{
					Ready: ready,
				},
			},
		},
	}
}

// TeamSendInvitationRpc 邀请玩家入队。teamKey 为 nil 时邀请进自己所在的默认队伍(不存在则服务端先创建)。
func TeamSendInvitationRpc(action base.TaskActionImpl, user user_data.User, invitee *public_protocol_pbdesc.DUserIDKey,
	teamKey *public_protocol_pbdesc.DTeamKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamSendInvitationRsp], error) {
	if invitee == nil || invitee.GetUserId() == 0 {
		return 0, nil, fmt.Errorf("invitee is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamSendInvitationReq{
		TeamKey:       teamKey,
		UserKey:       invitee,
		TeamSourceType: public_protocol_pbdesc.EnTeamSourceType_EN_TEAM_SOURCE_TYPE_FRIEND,
	}
	return lobbysvr_rpc_handle.SendTeamSendInvitation(action, user, csBody, true)
}

// TeamApproveInvitationRpc 接受收到的邀请(自己作为被邀请人)。
func TeamApproveInvitationRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamApproveInvitationRsp], error) {
	if teamKey == nil {
		return 0, nil, fmt.Errorf("team_key is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamApproveInvitationReq{
		TeamKey: teamKey,
		UserKey: BuildUserKey(user, user.GetUserId(), user.GetZoneId()),
	}
	return lobbysvr_rpc_handle.SendTeamApproveInvitation(action, user, csBody, true)
}

// TeamRejectInvitationRpc 拒绝收到的邀请(自己作为被邀请人)。
func TeamRejectInvitationRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamRejectInvitationRsp], error) {
	if teamKey == nil {
		return 0, nil, fmt.Errorf("team_key is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamRejectInvitationReq{
		TeamKey: teamKey,
		UserKey: BuildUserKey(user, user.GetUserId(), user.GetZoneId()),
	}
	return lobbysvr_rpc_handle.SendTeamRejectInvitation(action, user, csBody, true)
}

// TeamSendJoinRequestRpc 向指定队伍发起加入申请。
func TeamSendJoinRequestRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamSendJoinRequestRsp], error) {
	if teamKey == nil {
		return 0, nil, fmt.Errorf("team_key is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamSendJoinRequestReq{
		TeamKey:       teamKey,
		TeamSourceType: public_protocol_pbdesc.EnTeamSourceType_EN_TEAM_SOURCE_TYPE_NONE,
	}
	return lobbysvr_rpc_handle.SendTeamSendJoinRequest(action, user, csBody, true)
}

// TeamAcceptJoinRequestRpc 批准玩家的加入申请(需要审批权限)。
func TeamAcceptJoinRequestRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey,
	requester *public_protocol_pbdesc.DUserIDKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamAcceptJoinRequestRsp], error) {
	if teamKey == nil || requester == nil || requester.GetUserId() == 0 {
		return 0, nil, fmt.Errorf("team_key or requester is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamAcceptJoinRequestReq{
		TeamKey: teamKey,
		UserKey: requester,
	}
	return lobbysvr_rpc_handle.SendTeamAcceptJoinRequest(action, user, csBody, true)
}

// TeamRejectJoinRequestRpc 拒绝玩家的加入申请(需要审批权限)。
func TeamRejectJoinRequestRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey,
	requester *public_protocol_pbdesc.DUserIDKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamRejectJoinRequestRsp], error) {
	if teamKey == nil || requester == nil || requester.GetUserId() == 0 {
		return 0, nil, fmt.Errorf("team_key or requester is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamRejectJoinRequestReq{
		TeamKey: teamKey,
		UserKey: requester,
	}
	return lobbysvr_rpc_handle.SendTeamRejectJoinRequest(action, user, csBody, true)
}

// TeamExitRpc 退出队伍。退出成功后清空本地队伍视图。
func TeamExitRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamExitRsp], error) {
	if teamKey == nil {
		return 0, nil, fmt.Errorf("team_key is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamExitReq{
		TeamKey: teamKey,
	}
	return lobbysvr_rpc_handle.SendTeamExit(action, user, csBody, true)
}

// TeamRemoveMemberRpc 移出成员(需要管理权限); userKey 为自己时等价于主动退队。
func TeamRemoveMemberRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey,
	member *public_protocol_pbdesc.DUserIDKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamRemoveMemberRsp], error) {
	if teamKey == nil || member == nil || member.GetUserId() == 0 {
		return 0, nil, fmt.Errorf("team_key or member is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamRemoveMemberReq{
		TeamKey: teamKey,
		UserKey: member,
	}
	return lobbysvr_rpc_handle.SendTeamRemoveMember(action, user, csBody, true)
}

// TeamTransferCaptainRpc 转移队长(需要是队长或拥有 OWNER 权限)。
func TeamTransferCaptainRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey,
	member *public_protocol_pbdesc.DUserIDKey) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamTransferCaptainRsp], error) {
	if teamKey == nil || member == nil || member.GetUserId() == 0 {
		return 0, nil, fmt.Errorf("team_key or member is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamTransferCaptainReq{
		TeamKey: teamKey,
		UserKey: member,
	}
	return lobbysvr_rpc_handle.SendTeamTransferCaptain(action, user, csBody, true)
}

// TeamUpdateMemberRoleRpc 设置成员角色(默认需要 ADMIN 权限，不能授予高于自己的角色)。
func TeamUpdateMemberRoleRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey,
	member *public_protocol_pbdesc.DUserIDKey, role public_protocol_pbdesc.EnTeamPermissionRole) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamUpdateMemberRoleRsp], error) {
	if teamKey == nil || member == nil || member.GetUserId() == 0 {
		return 0, nil, fmt.Errorf("team_key or member is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamUpdateMemberRoleReq{
		TeamKey: teamKey,
		UserKey: member,
		Role:    role,
	}
	return lobbysvr_rpc_handle.SendTeamUpdateMemberRole(action, user, csBody, true)
}

// TeamUpdateMemberDataRpc 更新自己的成员共享数据(当前支持战斗模块 ready 状态)。
func TeamUpdateMemberDataRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey,
	ready bool) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamUpdateMemberDataRsp], error) {
	if teamKey == nil {
		return 0, nil, fmt.Errorf("team_key is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamUpdateMemberDataReq{
		TeamKey: teamKey,
		Data:    []*public_protocol_pbdesc.DTeamMemberSharedDataModule{BuildTeamMemberReadyData(ready)},
	}
	return lobbysvr_rpc_handle.SendTeamUpdateMemberData(action, user, csBody, true)
}

// TeamUpdateTeamDataRpc 更新队伍共享数据(当前支持战斗模块 matching 状态)。
func TeamUpdateTeamDataRpc(action base.TaskActionImpl, user user_data.User, teamKey *public_protocol_pbdesc.DTeamKey,
	matching bool) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCTeamUpdateTeamDataRsp], error) {
	if teamKey == nil {
		return 0, nil, fmt.Errorf("team_key is empty")
	}
	csBody := &public_protocol_pbdesc.CSTeamUpdateTeamDataReq{
		TeamKey: teamKey,
		Data:    []*public_protocol_pbdesc.DTeamSharedDataModule{BuildTeamMatchingData(matching)},
	}
	return lobbysvr_rpc_handle.SendTeamUpdateTeamData(action, user, csBody, true)
}

// RegisterTeamDirtySyncPushHandler 注册 SCUserDirtyChgSync 推送处理器，维护本地队伍视图并打印队伍变更。
func RegisterTeamDirtySyncPushHandler(user user_data.User) {
	lobbysvr_rpc_handle.RegisterMessageHandlerUserDirtyChgSync(user,
		func(action *user_data.TaskActionUser, msg *public_protocol_pbdesc.SCUserDirtyChgSync, errCode int32) error {
			if errCode < 0 {
				action.User.Log("[team] SCUserDirtyChgSync push error, errCode=%d", errCode)
				return nil
			}
			for _, dirtyTeam := range msg.GetDirtyTeam() {
				if snapshot := dirtyTeam.GetSnapshot(); snapshot != nil {
					SaveTeamSnapshot(action.User, snapshot)
					teamKey := snapshot.GetSnapshot().GetTeamKey()
					action.User.Log(
						"[team] push snapshot team=%d:%d, members=%d, pending_invitations=%d, pending_join_requests=%d, shared_team_data=%d",
						teamKey.GetZoneId(), teamKey.GetTeamId(), len(snapshot.GetSnapshot().GetMember()),
						len(snapshot.GetSnapshot().GetPendingInvitation()),
						len(snapshot.GetSnapshot().GetPendingJoinRequest()), len(snapshot.GetSharedTeamData()))
					continue
				}
				if increase := dirtyTeam.GetIncrease(); increase != nil {
					teamKey := increase.GetTeamKey()
					actions := make([]*public_protocol_pbdesc.DTeamAction, 0, len(increase.GetActions()))
					for _, oneAction := range increase.GetActions() {
						actions = append(actions, oneAction.GetActions())
						action.User.Log("[team] push increase team=%d:%d, action=%s, member_shared_data=%d",
							teamKey.GetZoneId(), teamKey.GetTeamId(), oneAction.GetActions().String(),
							len(oneAction.GetSharedMemberData()))
					}
					AppendTeamIncreaseActions(action.User, actions)
				}
			}
			return nil
		})
}
