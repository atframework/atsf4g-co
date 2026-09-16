package atsf4g_go_robot_task

import (
	"fmt"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"
)

func MatchingLevelSelectTask(task *user_data.TaskActionUser, levelIds []int32, region string,
	factionFillPolicy public_protocol_pbdesc.EnMatchingFactionFillPolicy) error {
	errCode, _, rpcErr := protocol.MatchingLevelSelectRpc(task, task.User, levelIds, region, factionFillPolicy)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("matching level select failed, errCode: %d", errCode)
	}
	task.Log("matching level select success, level_ids=%v region=%s fill_policy=%s", levelIds, region,
		factionFillPolicy.String())
	return nil
}

func MatchingStartTask(task *user_data.TaskActionUser) error {
	errCode, rspHolder, rpcErr := protocol.MatchingStartRpc(task, task.User)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("matching start failed, errCode: %d", errCode)
	}

	protocol.RegisterMatchingLogSyncHandler(task.User)
	if rspHolder == nil {
		return nil
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get matching start response message: %v", err)
	}
	protocol.SaveMatchingView(task.User, rsp.GetView())
	task.User.SetExtralData("MatchingFactionId", int32(0))
	task.Log("matching start success, unit_id=%d", rsp.GetView().GetUnitId())
	return nil
}

func MatchingCheckTask(task *user_data.TaskActionUser) error {
	return matchingCheckTask(task, false)
}

// MatchingCheckForWaitTask allows the short window before a team member receives the unit subscription event.
// Other business errors still fail the case immediately.
func MatchingCheckForWaitTask(task *user_data.TaskActionUser) error {
	return matchingCheckTask(task, true)
}

func matchingCheckTask(task *user_data.TaskActionUser, allowNotFound bool) error {
	errCode, rspHolder, rpcErr := protocol.MatchingCheckRpc(task, task.User)
	if rpcErr != nil {
		return rpcErr
	}
	if allowNotFound && errCode == int32(public_protocol_pbdesc.EnErrorCode_EN_MATCHING_RESULT_NOT_FOUND) {
		return nil
	}
	if errCode < 0 {
		return fmt.Errorf("matching check failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return nil
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get matching check response message: %v", err)
	}
	protocol.SaveMatchingView(task.User, rsp.GetView())
	return nil
}

func MatchingCancelTask(task *user_data.TaskActionUser) error {
	errCode, rspHolder, rpcErr := protocol.MatchingCancelRpc(task, task.User)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("matching cancel failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return nil
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get matching cancel response message: %v", err)
	}
	protocol.SaveMatchingView(task.User, rsp.GetView())
	return nil
}

func MatchingConfirmTask(task *user_data.TaskActionUser, confirmed bool) error {
	errCode, rspHolder, rpcErr := protocol.MatchingConfirmRpc(task, task.User, confirmed)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("matching confirm failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return nil
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get matching confirm response message: %v", err)
	}
	protocol.SaveMatchingView(task.User, rsp.GetView())
	return nil
}
