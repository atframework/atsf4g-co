package atsf4g_go_robot_case

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	robot_case "github.com/atframework/robot-go/case"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_case.RegisterCase("matching_level_select", MatchingLevelSelectCase, time.Second*30)
	robot_case.RegisterCase("matching_start", MatchingStartCase, time.Second*30)
	robot_case.RegisterCase("matching_wait", MatchingWaitCase, time.Minute*5)
	robot_case.RegisterCase("matching_wait_success", MatchingWaitSuccessCase, time.Minute*5)
	robot_case.RegisterCase("matching_confirm", MatchingConfirmCase, time.Second*30)
	robot_case.RegisterCase("matching_assert_faction", MatchingAssertFactionCase, time.Second*30)
}

func MatchingLevelSelectCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("need level_id")
	}
	levelId, err := strconv.ParseInt(args[0], 10, 32)
	if err != nil {
		return err
	}
	region := "cn"
	if len(args) > 1 {
		region = args[1]
	}
	factionFillPolicy := public_protocol_pbdesc.EnMatchingFactionFillPolicy_EN_MATCHING_FACTION_FILL_POLICY_DISABLE
	if len(args) > 2 {
		fillPolicyValue, parseErr := strconv.ParseInt(args[2], 10, 32)
		if parseErr != nil {
			return parseErr
		}
		factionFillPolicy = public_protocol_pbdesc.EnMatchingFactionFillPolicy(fillPolicyValue)
	}
	levelIds := []int32{int32(levelId)}
	if len(args) > 3 {
		levelIds = levelIds[:0]
		for _, value := range strings.Split(args[3], ",") {
			parsed, parseErr := strconv.ParseInt(value, 10, 32)
			if parseErr != nil {
				return parseErr
			}
			levelIds = append(levelIds, int32(parsed))
		}
	}

	user := holder.GetUser()
	if user == nil {
		return fmt.Errorf("user not initialized, run login first")
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.MatchingLevelSelectTask(taskAction, levelIds, region, factionFillPolicy)
	}, "Matching Level Select Task"))
}

func MatchingStartCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) > 0 {
		return fmt.Errorf("matching_start does not accept arguments; run matching_level_select first")
	}
	user := holder.GetUser()
	if user == nil {
		return fmt.Errorf("user not initialized, run login first")
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(task.MatchingStartTask, "Matching Start Task"))
}

func MatchingWaitCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	return matchingWaitCase(action, holder, args, false)
}

// MatchingWaitSuccessCase only accepts FINISHED; entering confirmation is not battle creation success.
func MatchingWaitSuccessCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	return matchingWaitCase(action, holder, args, true)
}

func matchingWaitSuccessStatus(status public_protocol_pbdesc.EnMatchingUnitLifecycleStatus) (bool, error) {
	switch status {
	case public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED:
		return true, nil
	case public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED,
		public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT,
		public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED:
		return false, fmt.Errorf("matching did not succeed, status: %s", status.String())
	default:
		return false, nil
	}
}

func matchingWaitCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string, requireSuccess bool) error {
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
		status := public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_INVALID
		var checkErr error
		if requireSuccess {
			// A NOT_FOUND response must not reuse a previous attempt's cached FINISHED state.
			checkErr = action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
				errCode, response, rpcErr := protocol.MatchingCheckRpc(taskAction, taskAction.User)
				if rpcErr != nil {
					return rpcErr
				}
				if errCode == int32(public_protocol_pbdesc.EnErrorCode_EN_MATCHING_RESULT_NOT_FOUND) {
					return nil
				}
				if errCode < 0 {
					return fmt.Errorf("matching check failed, errCode: %d", errCode)
				}
				if response == nil {
					return fmt.Errorf("matching check response missing")
				}
				message, responseErr := response.GetMessage()
				if responseErr != nil {
					return responseErr
				}
				if message.GetView() == nil || message.GetView().GetUnitId() == 0 {
					return fmt.Errorf("matching check view missing active unit")
				}
				status = message.GetView().GetStatus()
				protocol.SaveMatchingView(taskAction.User, message.GetView())
				return nil
			}, "Matching Success Check Task"))
		} else {
			checkErr = action.AwaitTask(user.RunTaskDefaultTimeout(task.MatchingCheckForWaitTask, "Matching Check Task"))
			status = protocol.MatchingStatusFromUser(user)
		}
		if checkErr != nil {
			return checkErr
		}
		if requireSuccess {
			finished, statusErr := matchingWaitSuccessStatus(status)
			if statusErr != nil {
				return statusErr
			}
			if finished {
				action.Log("matching succeeded, status: %s", status.String())
				return nil
			}
			time.Sleep(time.Duration(intervalSeconds) * time.Second)
			continue
		}
		switch status {
		case public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING,
			public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE,
			public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED,
			public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED,
			public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT,
			public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED:
			action.Log("matching wait finished, status: %s", status.String())
			return nil
		}
		time.Sleep(time.Duration(intervalSeconds) * time.Second)
	}
	return fmt.Errorf("matching wait timeout after %d seconds, status: %s",
		timeoutSeconds, protocol.MatchingStatusFromUser(user).String())
}

func MatchingAssertFactionCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("need expected faction_id")
	}
	expectedFactionId, err := strconv.ParseInt(args[0], 10, 32)
	if err != nil {
		return err
	}
	user := holder.GetUser()
	if user == nil {
		return fmt.Errorf("user not initialized, run login first")
	}
	if err = action.AwaitTask(user.RunTaskDefaultTimeout(task.MatchingCheckTask, "Matching Check Task")); err != nil {
		return err
	}
	actualFactionId := protocol.MatchingFactionIdFromUser(user)
	if actualFactionId != int32(expectedFactionId) {
		return fmt.Errorf("matching faction mismatch: expected %d, got %d, status: %s", expectedFactionId,
			actualFactionId, protocol.MatchingStatusFromUser(user).String())
	}
	action.Log("matching faction assertion passed: faction_id=%d", actualFactionId)
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
