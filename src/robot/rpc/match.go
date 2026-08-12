package atsf4g_go_robot_rpc

import (
	"fmt"

	pu "github.com/atframework/atframe-utils-go/proto_utility"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	user_data "github.com/atframework/robot-go/data"
)

func MatchingStartRpc(action base.TaskActionImpl, user user_data.User, levelType, levelId int32, region string, selectType public_protocol_pbdesc.EnMatchSelectSvrType) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingStartRsp], error) {
	csBody := &public_protocol_pbdesc.CSMatchingStartReq{
		LevelSelect: &public_protocol_pbdesc.DLevelSelect{
			LevelType: levelType,
			LevelId:   levelId,
			Region:    region,
		},
		SelectType: selectType,
		BattleVersion: "0.0.0.1",
	}
	return lobbysvr_rpc_handle.SendMatchingStart(action, user, csBody, true)
}

func MatchingCheckRpc(action base.TaskActionImpl, user user_data.User) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingCheckRsp], error) {
	matchingId, err := MatchingIdFromUser(user)
	if err != nil {
		return 0, nil, err
	}
	csBody := &public_protocol_pbdesc.CSMatchingCheckReq{
		MatchingId:         matchingId,
		UnitId:             UnitIdFromUser(user),
		AcknowledgeEventId: AcknowledgeEventIdFromUser(user),
	}
	return lobbysvr_rpc_handle.SendMatchingCheck(action, user, csBody, true)
}

func MatchingCancelRpc(action base.TaskActionImpl, user user_data.User) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingCancelRsp], error) {
	matchingId, err := MatchingIdFromUser(user)
	if err != nil {
		return 0, nil, err
	}
	csBody := &public_protocol_pbdesc.CSMatchingCancelReq{
		MatchingId: matchingId,
		UnitId:     UnitIdFromUser(user),
	}
	return lobbysvr_rpc_handle.SendMatchingCancel(action, user, csBody, true)
}

func MatchingConfirmRpc(action base.TaskActionImpl, user user_data.User, confirmed bool) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMatchingConfirmRsp], error) {
	matchingId, err := MatchingIdFromUser(user)
	if err != nil {
		return 0, nil, err
	}
	csBody := &public_protocol_pbdesc.CSMatchingConfirmReq{
		MatchingId: matchingId,
		UnitId:     UnitIdFromUser(user),
		Confirmed:  confirmed,
	}
	return lobbysvr_rpc_handle.SendMatchingConfirm(action, user, csBody, true)
}

func MatchingIdFromUser(user user_data.User) (string, error) {
	matchingId, ok := user.GetExtralData("MatchingId").(string)
	if !ok || matchingId == "" {
		return "", fmt.Errorf("matching not started")
	}
	return matchingId, nil
}

func UnitIdFromUser(user user_data.User) uint64 {
	unitId, _ := user.GetExtralData("UnitId").(uint64)
	return unitId
}

func AcknowledgeEventIdFromUser(user user_data.User) int64 {
	eventId, _ := user.GetExtralData("AcknowledgeEventId").(int64)
	return eventId
}

func MatchingStatusFromUser(user user_data.User) public_protocol_pbdesc.EnMatchingRoomStatus {
	status, _ := user.GetExtralData("MatchingStatus").(public_protocol_pbdesc.EnMatchingRoomStatus)
	return status
}

func SaveMatchingSnapshot(user user_data.User, matchingId string, unitId uint64, snapshot *public_protocol_pbdesc.DMatchingRoomSnapshot) {
	if matchingId != "" {
		user.SetExtralData("MatchingId", matchingId)
	}
	if unitId != 0 {
		user.SetExtralData("UnitId", unitId)
	}
	if snapshot == nil {
		return
	}
	if snapshot.GetMatchingId() != "" {
		user.SetExtralData("MatchingId", snapshot.GetMatchingId())
	}
	user.SetExtralData("AcknowledgeEventId", snapshot.GetLastEventId())
	user.SetExtralData("MatchingStatus", snapshot.GetStatus())
	user.Log("matching snapshot: matching_id=%s status=%s last_event_id=%d units=%d",
		snapshot.GetMatchingId(), snapshot.GetStatus().String(), snapshot.GetLastEventId(), len(snapshot.GetUnits()))
}

func RegisterMatchingLogSyncHandler(user user_data.User) {
	lobbysvr_rpc_handle.RegisterMessageHandlerMatchingLogSync(user, func(action *user_data.TaskActionUser, msg *public_protocol_pbdesc.SCMatchingLogSync, errCode int32) error {
		if errCode < 0 {
			return fmt.Errorf("matching log sync failed, errCode: %d", errCode)
		}
		SaveMatchingSnapshot(action.User, msg.GetMatchingId(), 0, msg.GetSnapshot())
		for _, event := range msg.GetEventLogs() {
			if event != nil {
				action.Log("matching log sync: event_id=%d room_status=%s",
					event.GetEventId(), event.GetRoomStatus().String())
			}
		}
		return nil
	})
}
