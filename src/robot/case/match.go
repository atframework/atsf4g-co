package atsf4g_go_robot_case

import (
	"fmt"
	"strconv"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	robot_case "github.com/atframework/robot-go/case"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_case.RegisterCase("matching_start", MatchingStartCase, time.Second*30)
	robot_case.RegisterCase("matching_wait", MatchingWaitCase, time.Minute*5)
	robot_case.RegisterCase("matching_confirm", MatchingConfirmCase, time.Second*30)
}

func MatchingStartCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("need level_type")
	}
	levelType, err := strconv.ParseInt(args[0], 10, 32)
	if err != nil {
		return err
	}
	levelId := levelType
	region := "cn"
	selectType := public_protocol_pbdesc.EnMatchSelectSvrType_EN_MATCH_SELECT_SVR_TYPE_PLAYER
	if len(args) > 1 {
		levelId, err = strconv.ParseInt(args[1], 10, 32)
		if err != nil {
			return err
		}
	}
	if len(args) > 2 {
		region = args[2]
	}
	if len(args) > 3 {
		selectTypeValue, parseErr := strconv.ParseInt(args[3], 10, 32)
		if parseErr != nil {
			return parseErr
		}
		selectType = public_protocol_pbdesc.EnMatchSelectSvrType(selectTypeValue)
	}

	user := holder.GetUser()
	if user == nil {
		return fmt.Errorf("user not initialized, run login first")
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.MatchingStartTask(taskAction, int32(levelType), int32(levelId), region, selectType)
	}, "Matching Start Task"))
}

func MatchingWaitCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("need timeout seconds")
	}
	timeoutSeconds, err := strconv.ParseInt(args[0], 10, 64)
	if err != nil {
		return err
	}
	intervalSeconds := int64(2)
	if len(args) > 1 {
		intervalSeconds, err = strconv.ParseInt(args[1], 10, 64)
		if err != nil {
			return err
		}
	}
	if intervalSeconds <= 0 {
		intervalSeconds = 1
	}

	user := holder.GetUser()
	if user == nil {
		return fmt.Errorf("user not initialized, run login first")
	}
	deadline := time.Now().Add(time.Duration(timeoutSeconds) * time.Second)
	for time.Now().Before(deadline) {
		err := action.AwaitTask(user.RunTaskDefaultTimeout(task.MatchingCheckTask, "Matching Check Task"))
		if err != nil {
			return err
		}
		status := protocol.MatchingStatusFromUser(user)
		switch status {
		case public_protocol_pbdesc.EnMatchingRoomStatus_EN_MATCHING_ROOM_STATUS_CREATING_BATTLE,
			public_protocol_pbdesc.EnMatchingRoomStatus_EN_MATCHING_ROOM_STATUS_FINISHED,
			public_protocol_pbdesc.EnMatchingRoomStatus_EN_MATCHING_ROOM_STATUS_CANCELLED,
			public_protocol_pbdesc.EnMatchingRoomStatus_EN_MATCHING_ROOM_STATUS_TIMEOUT,
			public_protocol_pbdesc.EnMatchingRoomStatus_EN_MATCHING_ROOM_STATUS_FAILED:
			action.Log("matching wait finished, status: %s", status.String())
			return nil
		}
		time.Sleep(time.Duration(intervalSeconds) * time.Second)
	}
	action.Log("matching wait timeout after %d seconds, status: %s",
		timeoutSeconds, protocol.MatchingStatusFromUser(user).String())
	return nil
}

func MatchingConfirmCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	confirmed := true
	if len(args) > 0 {
		var err error
		confirmed, err = strconv.ParseBool(args[0])
		if err != nil {
			return err
		}
	}
	user := holder.GetUser()
	if user == nil {
		return fmt.Errorf("user not initialized, run login first")
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.MatchingConfirmTask(taskAction, confirmed)
	}, "Matching Confirm Task"))
}
