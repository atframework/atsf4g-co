package atsf4g_go_robot_rpc

import (
	"fmt"

	pu "github.com/atframework/atframe-utils-go/proto_utility"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	user_data "github.com/atframework/robot-go/data"
)

func MatchingStartRpc(action base.TaskActionImpl, user user_data.User, levelType, preferredLevelId int32,
	levelIds []int32, region string,
	factionFillPolicy public_protocol_pbdesc.EnMatchingFactionFillPolicy) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingStartRsp], error) {
	csBody := &public_protocol_pbdesc.CSMatchingStartReq{
		LevelSelect: &public_protocol_pbdesc.DLevelSelect{
			LevelIds:  levelIds,
			Region:    region,
		},
		BattleVersion:     "0.0.0.1",
		FactionFillPolicy: factionFillPolicy,
	}
	return lobbysvr_rpc_handle.SendMatchingStart(action, user, csBody, true)
}

func MatchingCheckRpc(action base.TaskActionImpl, user user_data.User) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingCheckRsp], error) {
	csBody := &public_protocol_pbdesc.CSMatchingCheckReq{}
	return lobbysvr_rpc_handle.SendMatchingCheck(action, user, csBody, true)
}

func MatchingCancelRpc(action base.TaskActionImpl, user user_data.User) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingCancelRsp], error) {
	csBody := &public_protocol_pbdesc.CSMatchingCancelReq{
		UnitId: UnitIdFromUser(user),
	}
	return lobbysvr_rpc_handle.SendMatchingCancel(action, user, csBody, true)
}

func MatchingConfirmRpc(action base.TaskActionImpl, user user_data.User, confirmed bool) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingConfirmRsp], error) {
	csBody := &public_protocol_pbdesc.CSMatchingConfirmReq{
		UnitId:    UnitIdFromUser(user),
		Confirmed: confirmed,
	}
	return lobbysvr_rpc_handle.SendMatchingConfirm(action, user, csBody, true)
}

func UnitIdFromUser(user user_data.User) uint64 {
	unitId, _ := user.GetExtralData("UnitId").(uint64)
	return unitId
}

func MatchingStatusFromUser(user user_data.User) public_protocol_pbdesc.EnMatchingRoomStatus {
	status, _ := user.GetExtralData("MatchingStatus").(public_protocol_pbdesc.EnMatchingRoomStatus)
	return status
}

func MatchingFactionIdFromUser(user user_data.User) int32 {
	factionId, _ := user.GetExtralData("MatchingFactionId").(int32)
	return factionId
}

func SaveMatchingView(user user_data.User, view *public_protocol_pbdesc.DMatchingClientView) {
	if view == nil {
		return
	}
	currentViewRevision, _ := user.GetExtralData("MatchingViewRevision").(uint64)
	if view.GetViewRevision() < currentViewRevision {
		user.Log("ignore stale matching view: view_revision=%d current_view_revision=%d", view.GetViewRevision(),
			currentViewRevision)
		return
	}
	if view.GetUnitId() != 0 {
		user.SetExtralData("UnitId", view.GetUnitId())
	}
	user.SetExtralData("MatchingViewRevision", view.GetViewRevision())
	user.SetExtralData("MatchingStatus", view.GetStatus())
	// user.SetExtralData("MatchingFactionId", view.GetFactionId())
	user.Log("matching view: view_revision=%d status=%s unit_id=%d", view.GetViewRevision(),
		view.GetStatus().String(), view.GetUnitId())
}

func RegisterMatchingLogSyncHandler(user user_data.User) {
	lobbysvr_rpc_handle.RegisterMessageHandlerMatchingLogSync(user, func(action *user_data.TaskActionUser, msg *public_protocol_pbdesc.SCMatchingLogSync, errCode int32) error {
		if errCode < 0 {
			return fmt.Errorf("matching log sync failed, errCode: %d", errCode)
		}
		SaveMatchingView(action.User, msg.GetClientView())
		return nil
	})
}
