package atsf4g_go_robot_case

import (
	"fmt"
	"strconv"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	public_common_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/common"
	robot_case "github.com/atframework/robot-go/case"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_case.RegisterCase("team_invite", TeamInviteCase, time.Second*30)
	robot_case.RegisterCase("team_accept_invitation", TeamAcceptInvitationCase, time.Second*30)
	robot_case.RegisterCase("team_wait_members", TeamWaitMembersCase, time.Minute*2)
	robot_case.RegisterCase("team_ready", TeamReadyCase, time.Second*30)
}

func requireCaseUser(holder *user_data.UserHolder) (user_data.User, error) {
	user := holder.GetUser()
	if user == nil {
		return nil, fmt.Errorf("user not initialized, run login first")
	}
	return user, nil
}

func currentTeamKey(user user_data.User) (*public_common_pbdesc.DTeamKey, error) {
	view := protocol.GetTeamView(user)
	if view.Snapshot == nil || view.Snapshot.GetSnapshot().GetTeamKey().GetTeamId() == 0 {
		return nil, fmt.Errorf("no cached team, run team pull or team_wait_members first")
	}
	return view.Snapshot.GetSnapshot().GetTeamKey(), nil
}

// TeamInviteCase 的参数是被邀请玩家的完整 OpenID。
func TeamInviteCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 1 {
		return fmt.Errorf("need exactly one invitee open_id")
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	inviteeHolder := user_data.UserContainerTryGetUser(args[0])
	if inviteeHolder == nil || inviteeHolder.GetUser() == nil || !inviteeHolder.GetUser().IsLogin() {
		return fmt.Errorf("invitee %q is not logged in", args[0])
	}
	invitee := inviteeHolder.GetUser()
	inviteeKey := protocol.BuildUserKey(user, invitee.GetUserId(), invitee.GetZoneId())
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.TeamSendInvitationTask(taskAction, inviteeKey, nil,
			public_common_pbdesc.EnTeamType_EN_TEAM_TYPE_NORMAL)
	}, "Team Invite Task"))
}

// TeamAcceptInvitationCase 拉取服务端待处理邀请，并接受唯一一条邀请。
func TeamAcceptInvitationCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 0 {
		return fmt.Errorf("team_accept_invitation does not accept arguments")
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	if err = action.AwaitTask(user.RunTaskDefaultTimeout(task.TeamPullInfoTask, "Team Pull Invitation Task")); err != nil {
		return err
	}
	invitations := protocol.GetTeamView(user).PendingInvitations
	if len(invitations) != 1 {
		return fmt.Errorf("expected exactly one pending team invitation, got %d", len(invitations))
	}
	teamKey := invitations[0].GetTeamKey()
	if err = action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.TeamApproveInvitationTask(taskAction, teamKey)
	}, "Team Accept Invitation Task")); err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(task.TeamPullInfoTask, "Team Pull Accepted Team Task"))
}

func TeamWaitMembersCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) < 1 || len(args) > 3 {
		return fmt.Errorf("need expected member count, optional timeout seconds and interval milliseconds")
	}
	expected, err := strconv.Atoi(args[0])
	if err != nil || expected <= 0 {
		return fmt.Errorf("invalid expected member count %q", args[0])
	}
	timeout := 30 * time.Second
	if len(args) > 1 {
		seconds, parseErr := strconv.Atoi(args[1])
		if parseErr != nil || seconds <= 0 {
			return fmt.Errorf("invalid timeout seconds %q", args[1])
		}
		timeout = time.Duration(seconds) * time.Second
	}
	interval := 500 * time.Millisecond
	if len(args) > 2 {
		milliseconds, parseErr := strconv.Atoi(args[2])
		if parseErr != nil || milliseconds <= 0 {
			return fmt.Errorf("invalid interval milliseconds %q", args[2])
		}
		interval = time.Duration(milliseconds) * time.Millisecond
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	deadline := time.Now().Add(timeout)
	for {
		if err = action.AwaitTask(user.RunTaskDefaultTimeout(task.TeamPullInfoTask, "Team Pull Members Task")); err != nil {
			return err
		}
		view := protocol.GetTeamView(user)
		if view.Snapshot != nil && len(view.Snapshot.GetSnapshot().GetMember()) == expected {
			action.Log("team member count reached %d", expected)
			return nil
		}
		if !time.Now().Before(deadline) {
			actual := 0
			if view.Snapshot != nil {
				actual = len(view.Snapshot.GetSnapshot().GetMember())
			}
			return fmt.Errorf("team member count timeout: expected %d, got %d", expected, actual)
		}
		time.Sleep(interval)
	}
}

func TeamReadyCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 0 {
		return fmt.Errorf("team_ready does not accept arguments")
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	if err = action.AwaitTask(user.RunTaskDefaultTimeout(task.TeamPullInfoTask, "Team Pull Before Ready Task")); err != nil {
		return err
	}
	teamKey, err := currentTeamKey(user)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.TeamUpdateMemberDataTask(taskAction, teamKey, true)
	}, "Team Ready Task"))
}
