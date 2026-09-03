package atsf4g_go_robot_cmd

import (
	"fmt"
	"strconv"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	public_common_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/common"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	robot_cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_cmd.RegisterUserCommand([]string{"team", "invite"}, TeamInviteCmd,
		"<user_id> [zone_id]", "邀请玩家入队(没有队伍时服务端先创建)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "join"}, TeamSendJoinRequestCmd,
		"<team_id> [zone_id]", "向队伍发起加入申请", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "accept"}, TeamApproveInvitationCmd,
		"<team_id> [zone_id]", "接受队伍邀请(作为被邀请人)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "refuse"}, TeamRejectInvitationCmd,
		"<team_id> [zone_id]", "拒绝队伍邀请(作为被邀请人)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "accept_join"}, TeamAcceptJoinRequestCmd,
		"<user_id> [team_id] [zone_id]", "批准玩家的加入申请(需要审批权限)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "reject_join"}, TeamRejectJoinRequestCmd,
		"<user_id> [team_id] [zone_id]", "拒绝玩家的加入申请(需要审批权限)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "exit"}, TeamExitCmd,
		"[team_id] [zone_id]", "退出当前队伍", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "kick"}, TeamRemoveMemberCmd,
		"<user_id> [team_id] [zone_id]", "移出成员(需要管理权限)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "transfer"}, TeamTransferCaptainCmd,
		"<user_id> [team_id] [zone_id]", "转移队长", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "set_role"}, TeamUpdateMemberRoleCmd,
		"<user_id> <role> [team_id] [zone_id]", "设置成员角色(role: member/admin/owner 或 100/200/300)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "ready"}, TeamUpdateMemberDataCmd,
		"<true|false> [team_id] [zone_id]", "更新自己的准备状态(成员共享数据)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "matching"}, TeamUpdateTeamDataCmd,
		"<true|false> [team_id] [zone_id]", "更新队伍匹配状态(队伍共享数据)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "pull"}, TeamPullInfoCmd,
		"", "拉取队伍快照(user_get_info need_user_team)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"team", "show"}, TeamShowCmd,
		"", "打印本地缓存的队伍视图", nil, cmdDefaultTimeout)
}

// resolveTeamKey 从参数中解析 team_id/[zone_id]，缺省时回落到本地缓存视图的队伍 Key。
func resolveTeamKey(user user_data.User, cmd []string, index int) (*public_common_pbdesc.DTeamKey, error) {
	var teamId int64 = 0
	var zoneId uint32 = 0
	if len(cmd) > index && cmd[index] != "" {
		v, err := strconv.ParseInt(cmd[index], 10, 64)
		if err != nil || v < 0 {
			return nil, fmt.Errorf("invalid team_id %q", cmd[index])
		}
		teamId = v
	}
	if len(cmd) > index+1 && cmd[index+1] != "" {
		v, err := strconv.ParseUint(cmd[index+1], 10, 32)
		if err != nil {
			return nil, fmt.Errorf("invalid zone_id %q", cmd[index+1])
		}
		zoneId = uint32(v)
	}
	if teamId != 0 {
		return protocol.BuildTeamKey(user, teamId, zoneId), nil
	}
	if zoneId != 0 {
		return nil, fmt.Errorf("zone_id requires explicit team_id")
	}
	if snapshot := protocol.GetTeamView(user).Snapshot; snapshot != nil {
		if teamKey := snapshot.GetSnapshot().GetTeamKey(); teamKey != nil && teamKey.GetTeamId() != 0 {
			return teamKey, nil
		}
	}
	return nil, fmt.Errorf("need team_id (no cached team view, use 'user team pull' first)")
}

// parseUserKey 解析 <user_id> [zone_id] 为用户 Key。
func parseUserKey(user user_data.User, cmd []string, index int) (*public_protocol_pbdesc.DUserIDKey, error) {
	if len(cmd) <= index || cmd[index] == "" {
		return nil, fmt.Errorf("need user_id")
	}
	userId, err := strconv.ParseUint(cmd[index], 10, 64)
	if err != nil || userId == 0 {
		return nil, fmt.Errorf("invalid user_id %q", cmd[index])
	}
	var zoneId uint32 = 0
	// 若下一个参数是区服 ID(小整数)则消费掉，否则留给 team_id 解析
	if len(cmd) > index+1 && cmd[index+1] != "" {
		if v, err := strconv.ParseUint(cmd[index+1], 10, 32); err == nil && v > 0 && v <= 0xFFFF {
			zoneId = uint32(v)
		}
	}
	return protocol.BuildUserKey(user, userId, zoneId), nil
}

func parseTeamRole(cmd []string, index int) (public_common_pbdesc.EnTeamPermissionRole, error) {
	if len(cmd) <= index || cmd[index] == "" {
		return 0, fmt.Errorf("need role(member/admin/owner 或 100/200/300)")
	}
	switch cmd[index] {
	case "guest", "0":
		return public_common_pbdesc.EnTeamPermissionRole_EN_TEAM_MEMBER_ROLE_GUEST, nil
	case "member", "normal", "100":
		return public_common_pbdesc.EnTeamPermissionRole_EN_TEAM_MEMBER_ROLE_NORMAL, nil
	case "admin", "200":
		return public_common_pbdesc.EnTeamPermissionRole_EN_TEAM_MEMBER_ROLE_ADMIN, nil
	case "owner", "300":
		return public_common_pbdesc.EnTeamPermissionRole_EN_TEAM_MEMBER_ROLE_OWNER, nil
	}
	return 0, fmt.Errorf("invalid role %q(member/admin/owner 或 100/200/300)", cmd[index])
}

func parseBoolArg(cmd []string, index int) (bool, error) {
	if len(cmd) <= index || cmd[index] == "" {
		return false, fmt.Errorf("need true|false")
	}
	switch cmd[index] {
	case "true", "1", "yes", "on":
		return true, nil
	case "false", "0", "no", "off":
		return false, nil
	}
	return false, fmt.Errorf("invalid bool %q", cmd[index])
}

func TeamInviteCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	invitee, err := parseUserKey(user, cmd, 0)
	if err != nil {
		return err
	}
	// 邀请进自己所在的默认队伍(team_key 为空由服务端创建/复用)
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamSendInvitationTask(t, invitee, nil)
	}, "Team Send Invitation Task"))
}

func TeamSendJoinRequestCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	teamKey, err := resolveTeamKey(user, cmd, 0)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamSendJoinRequestTask(t, teamKey)
	}, "Team Send Join Request Task"))
}

func TeamApproveInvitationCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	teamKey, err := resolveTeamKey(user, cmd, 0)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamApproveInvitationTask(t, teamKey)
	}, "Team Approve Invitation Task"))
}

func TeamRejectInvitationCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	teamKey, err := resolveTeamKey(user, cmd, 0)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamRejectInvitationTask(t, teamKey)
	}, "Team Reject Invitation Task"))
}

func TeamAcceptJoinRequestCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 {
		return fmt.Errorf("need user_id")
	}
	requesterId, err := strconv.ParseUint(cmd[0], 10, 64)
	if err != nil || requesterId == 0 {
		return fmt.Errorf("invalid user_id %q", cmd[0])
	}
	teamKey, err := resolveTeamKey(user, cmd, 1)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamAcceptJoinRequestTask(t, teamKey, protocol.BuildUserKey(user, requesterId, 0))
	}, "Team Accept Join Request Task"))
}

func TeamRejectJoinRequestCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 {
		return fmt.Errorf("need user_id")
	}
	requesterId, err := strconv.ParseUint(cmd[0], 10, 64)
	if err != nil || requesterId == 0 {
		return fmt.Errorf("invalid user_id %q", cmd[0])
	}
	teamKey, err := resolveTeamKey(user, cmd, 1)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamRejectJoinRequestTask(t, teamKey, protocol.BuildUserKey(user, requesterId, 0))
	}, "Team Reject Join Request Task"))
}

func TeamExitCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	teamKey, err := resolveTeamKey(user, cmd, 0)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamExitTask(t, teamKey)
	}, "Team Exit Task"))
}

func TeamRemoveMemberCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 {
		return fmt.Errorf("need user_id")
	}
	memberId, err := strconv.ParseUint(cmd[0], 10, 64)
	if err != nil || memberId == 0 {
		return fmt.Errorf("invalid user_id %q", cmd[0])
	}
	teamKey, err := resolveTeamKey(user, cmd, 1)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamRemoveMemberTask(t, teamKey, protocol.BuildUserKey(user, memberId, 0))
	}, "Team Remove Member Task"))
}

func TeamTransferCaptainCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 {
		return fmt.Errorf("need user_id")
	}
	memberId, err := strconv.ParseUint(cmd[0], 10, 64)
	if err != nil || memberId == 0 {
		return fmt.Errorf("invalid user_id %q", cmd[0])
	}
	teamKey, err := resolveTeamKey(user, cmd, 1)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamTransferCaptainTask(t, teamKey, protocol.BuildUserKey(user, memberId, 0))
	}, "Team Transfer Captain Task"))
}

func TeamUpdateMemberRoleCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 2 {
		return fmt.Errorf("need user_id and role")
	}
	memberId, err := strconv.ParseUint(cmd[0], 10, 64)
	if err != nil || memberId == 0 {
		return fmt.Errorf("invalid user_id %q", cmd[0])
	}
	role, err := parseTeamRole(cmd, 1)
	if err != nil {
		return err
	}
	teamKey, err := resolveTeamKey(user, cmd, 2)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamUpdateMemberRoleTask(t, teamKey, protocol.BuildUserKey(user, memberId, 0), role)
	}, "Team Update Member Role Task"))
}

func TeamUpdateMemberDataCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	ready, err := parseBoolArg(cmd, 0)
	if err != nil {
		return err
	}
	teamKey, err := resolveTeamKey(user, cmd, 1)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamUpdateMemberDataTask(t, teamKey, ready)
	}, "Team Update Member Data Task"))
}

func TeamUpdateTeamDataCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	matching, err := parseBoolArg(cmd, 0)
	if err != nil {
		return err
	}
	teamKey, err := resolveTeamKey(user, cmd, 1)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamUpdateTeamDataTask(t, teamKey, matching)
	}, "Team Update Team Data Task"))
}

func TeamPullInfoCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.TeamPullInfoTask(t)
	}, "Team Pull Info Task"))
}

func TeamShowCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	view := protocol.GetTeamView(user)
	if view.Snapshot == nil {
		user.Log("[team] no cached team view, try 'user team pull' first")
		return nil
	}
	snapshot := view.Snapshot.GetSnapshot()
	teamKey := snapshot.GetTeamKey()
	user.Log("[team] team=%d:%d, captain=%d:%d, members=%d", teamKey.GetZoneId(), teamKey.GetTeamId(),
		snapshot.GetCaptainUserKey().GetZoneId(), snapshot.GetCaptainUserKey().GetUserId(), len(snapshot.GetMember()))
	for _, member := range snapshot.GetMember() {
		user.Log("[team]   member=%d:%d, role=%d, shared_member_data=%d", member.GetUserKey().GetZoneId(),
			member.GetUserKey().GetUserId(), int32(member.GetRole()), len(member.GetSharedMemberData()))
	}
	for _, invitation := range snapshot.GetPendingInvitation() {
		user.Log("[team]   pending_invitation invitee=%d:%d, expired=%v", invitation.GetInvitee().GetZoneId(),
			invitation.GetInvitee().GetUserId(), invitation.GetExpiredTimepoint())
	}
	for _, joinRequest := range snapshot.GetPendingJoinRequest() {
		user.Log("[team]   pending_join_request requester=%d:%d, expired=%v", joinRequest.GetRequester().GetZoneId(),
			joinRequest.GetRequester().GetUserId(), joinRequest.GetExpiredTimepoint())
	}
	for _, sharedData := range view.Snapshot.GetSharedTeamData() {
		user.Log("[team]   shared_team_data: %v", sharedData)
	}
	user.Log("[team] dirty_sync_count=%d, last_actions=%d", view.DirtySyncSeq, len(view.LastActions))
	return nil
}
