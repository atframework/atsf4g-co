package atsf4g_go_robot_cmd

import (
	"fmt"
	"strconv"

	task "github.com/atframework/atsf4g-co-robot/task"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	robot_cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_cmd.RegisterUserCommand([]string{"user", "matching", "start"}, MatchingStartCmd, "<level_type> [level_id] [region] [select_type]", "开始匹配", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"user", "matching", "check"}, MatchingCheckCmd, "", "查询匹配状态", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"user", "matching", "cancel"}, MatchingCancelCmd, "", "取消匹配", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"user", "matching", "confirm"}, MatchingConfirmCmd, "<true|false>", "确认匹配", nil, cmdDefaultTimeout)
}

func MatchingStartCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 {
		return fmt.Errorf("need level_type")
	}
	levelType, err := strconv.ParseInt(cmd[0], 10, 32)
	if err != nil {
		return err
	}
	levelId := levelType
	if len(cmd) > 1 {
		levelId, err = strconv.ParseInt(cmd[1], 10, 32)
		if err != nil {
			return err
		}
	}
	region := "cn"
	if len(cmd) > 2 {
		region = cmd[2]
	}
	selectType := public_protocol_pbdesc.EnMatchSelectSvrType_EN_MATCH_SELECT_SVR_TYPE_PLAYER
	if len(cmd) > 3 {
		selectTypeValue, parseErr := strconv.ParseInt(cmd[3], 10, 32)
		if parseErr != nil {
			return parseErr
		}
		selectType = public_protocol_pbdesc.EnMatchSelectSvrType(selectTypeValue)
	}

	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.MatchingStartTask(taskAction, int32(levelType), int32(levelId), region, selectType)
	}, "Matching Start Task"))
}

func MatchingCheckCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	return action.AwaitTask(user.RunTaskDefaultTimeout(task.MatchingCheckTask, "Matching Check Task"))
}

func MatchingCancelCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	return action.AwaitTask(user.RunTaskDefaultTimeout(task.MatchingCancelTask, "Matching Cancel Task"))
}

func MatchingConfirmCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	confirmed := true
	if len(cmd) > 0 {
		var err error
		confirmed, err = strconv.ParseBool(cmd[0])
		if err != nil {
			return err
		}
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.MatchingConfirmTask(taskAction, confirmed)
	}, "Matching Confirm Task"))
}
