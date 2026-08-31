package atsf4g_go_robot_task

import (
	"fmt"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"
)

func MatchingStartTask(task *user_data.TaskActionUser, levelIds []int32, region string,
	factionFillPolicy public_protocol_pbdesc.EnMatchingFactionFillPolicy) error {
	errCode, rspHolder, rpcErr := protocol.MatchingStartRpc(task, task.User, levelIds, region, factionFillPolicy)
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
	errCode, rspHolder, rpcErr := protocol.MatchingCheckRpc(task, task.User)
	if rpcErr != nil {
		return rpcErr
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
