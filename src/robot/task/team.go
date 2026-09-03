package atsf4g_go_robot_task

import (
	"fmt"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	public_common_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/common"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"
)

// TeamSendInvitationTask 邀请玩家入队(无队伍时服务端会先创建)。
func TeamSendInvitationTask(task *user_data.TaskActionUser, invitee *public_protocol_pbdesc.DUserIDKey,
	teamKey *public_common_pbdesc.DTeamKey, teamType public_common_pbdesc.EnTeamType) error {
	errCode, rspHolder, rpcErr := protocol.TeamSendInvitationRpc(task, task.User, invitee, teamKey, teamType)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team send invitation failed, errCode: %d", errCode)
	}
	protocol.RegisterTeamDirtySyncPushHandler(task.User)
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team send invitation response message: %v", err)
		}
	}
	task.Log("team send invitation to %d:%d success", invitee.GetZoneId(), invitee.GetUserId())
	return nil
}

// TeamApproveInvitationTask 接受邀请。
func TeamApproveInvitationTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamApproveInvitationRpc(task, task.User, teamKey)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team approve invitation failed, errCode: %d", errCode)
	}
	protocol.RegisterTeamDirtySyncPushHandler(task.User)
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team approve invitation response message: %v", err)
		}
	}
	task.Log("team approve invitation success, team=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId())
	return nil
}

// TeamRejectInvitationTask 拒绝邀请。
func TeamRejectInvitationTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamRejectInvitationRpc(task, task.User, teamKey)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team reject invitation failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team reject invitation response message: %v", err)
		}
	}
	task.Log("team reject invitation success, team=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId())
	return nil
}

// TeamSendJoinRequestTask 发起加入申请。
func TeamSendJoinRequestTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamSendJoinRequestRpc(task, task.User, teamKey)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team send join request failed, errCode: %d", errCode)
	}
	protocol.RegisterTeamDirtySyncPushHandler(task.User)
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team send join request response message: %v", err)
		}
	}
	task.Log("team send join request success, team=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId())
	return nil
}

// TeamAcceptJoinRequestTask 批准加入申请。
func TeamAcceptJoinRequestTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey,
	requester *public_protocol_pbdesc.DUserIDKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamAcceptJoinRequestRpc(task, task.User, teamKey, requester)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team accept join request failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team accept join request response message: %v", err)
		}
	}
	task.Log("team accept join request success, team=%d:%d, requester=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId(),
		requester.GetZoneId(), requester.GetUserId())
	return nil
}

// TeamRejectJoinRequestTask 拒绝加入申请。
func TeamRejectJoinRequestTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey,
	requester *public_protocol_pbdesc.DUserIDKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamRejectJoinRequestRpc(task, task.User, teamKey, requester)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team reject join request failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team reject join request response message: %v", err)
		}
	}
	task.Log("team reject join request success, team=%d:%d, requester=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId(),
		requester.GetZoneId(), requester.GetUserId())
	return nil
}

// TeamExitTask 退出队伍，成功后清空本地队伍视图。
func TeamExitTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamExitRpc(task, task.User, teamKey)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team exit failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team exit response message: %v", err)
		}
	}
	protocol.ClearTeamView(task.User)
	task.Log("team exit success, team=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId())
	return nil
}

// TeamRemoveMemberTask 移出成员。
func TeamRemoveMemberTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey,
	member *public_protocol_pbdesc.DUserIDKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamRemoveMemberRpc(task, task.User, teamKey, member)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team remove member failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team remove member response message: %v", err)
		}
	}
	// 移除自己等价于主动退队
	if member.GetUserId() == task.User.GetUserId() && member.GetZoneId() == task.User.GetZoneId() {
		protocol.ClearTeamView(task.User)
	}
	task.Log("team remove member success, team=%d:%d, member=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId(),
		member.GetZoneId(), member.GetUserId())
	return nil
}

// TeamTransferCaptainTask 转移队长。
func TeamTransferCaptainTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey,
	member *public_protocol_pbdesc.DUserIDKey) error {
	errCode, rspHolder, rpcErr := protocol.TeamTransferCaptainRpc(task, task.User, teamKey, member)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team transfer captain failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team transfer captain response message: %v", err)
		}
	}
	task.Log("team transfer captain success, team=%d:%d, new captain=%d:%d", teamKey.GetZoneId(), teamKey.GetTeamId(),
		member.GetZoneId(), member.GetUserId())
	return nil
}

// TeamUpdateMemberRoleTask 设置成员角色。
func TeamUpdateMemberRoleTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey,
	member *public_protocol_pbdesc.DUserIDKey, role public_common_pbdesc.EnTeamPermissionRole) error {
	errCode, rspHolder, rpcErr := protocol.TeamUpdateMemberRoleRpc(task, task.User, teamKey, member, role)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team update member role failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team update member role response message: %v", err)
		}
	}
	task.Log("team update member role success, team=%d:%d, member=%d:%d, role=%d", teamKey.GetZoneId(),
		teamKey.GetTeamId(), member.GetZoneId(), member.GetUserId(), int32(role))
	return nil
}

// TeamUpdateMemberDataTask 更新自己的成员共享数据(ready 状态)。
func TeamUpdateMemberDataTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey,
	ready bool) error {
	errCode, rspHolder, rpcErr := protocol.TeamUpdateMemberDataRpc(task, task.User, teamKey, ready)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team update member data failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team update member data response message: %v", err)
		}
	}
	task.Log("team update member data success, team=%d:%d, ready=%v", teamKey.GetZoneId(), teamKey.GetTeamId(), ready)
	return nil
}

// TeamUpdateTeamDataTask 更新队伍共享数据(matching 状态)。
func TeamUpdateTeamDataTask(task *user_data.TaskActionUser, teamKey *public_common_pbdesc.DTeamKey,
	matching bool) error {
	errCode, rspHolder, rpcErr := protocol.TeamUpdateTeamDataRpc(task, task.User, teamKey, matching)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team update team data failed, errCode: %d", errCode)
	}
	if rspHolder != nil {
		if _, err := rspHolder.GetMessage(); err != nil {
			return fmt.Errorf("failed to get team update team data response message: %v", err)
		}
	}
	task.Log("team update team data success, team=%d:%d, matching=%v", teamKey.GetZoneId(), teamKey.GetTeamId(),
		matching)
	return nil
}

// TeamPullInfoTask 主动拉取队伍快照(user_get_info need_user_team)，并更新本地队伍视图。
func TeamPullInfoTask(task *user_data.TaskActionUser) error {
	errCode, rspHolder, rpcErr := protocol.GetInfoRpc(task, task.User, []string{"team"})
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("team pull info failed, errCode: %d", errCode)
	}
	protocol.RegisterTeamDirtySyncPushHandler(task.User)
	if rspHolder == nil {
		return nil
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get user get info response message: %v", err)
	}
	for _, snapshot := range rsp.GetUserTeam() {
		protocol.SaveTeamSnapshot(task.User, snapshot)
		teamKey := snapshot.GetSnapshot().GetTeamKey()
		task.Log("team pull info, team=%d:%d, members=%d, pending_invitations=%d, pending_join_requests=%d",
			teamKey.GetZoneId(), teamKey.GetTeamId(), len(snapshot.GetSnapshot().GetMember()),
			len(snapshot.GetSnapshot().GetPendingInvitation()), len(snapshot.GetSnapshot().GetPendingJoinRequest()))
	}
	if len(rsp.GetUserTeam()) == 0 {
		protocol.ClearTeamView(task.User)
		task.Log("team pull info, not in any team")
	}
	return nil
}
