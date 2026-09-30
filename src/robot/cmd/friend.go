package atsf4g_go_robot_cmd

import (
	"fmt"
	"strconv"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	robot_cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_cmd.RegisterUserCommand([]string{"friend", "get_all"}, FriendGetAllCmd, "", "拉取好友、申请和礼物列表", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"friend", "invite"}, FriendInviteCmd, "<user_id> [zone_id]", "发送好友申请", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"friend", "accept"}, FriendAcceptCmd, "<user_id> [zone_id]", "接受好友申请", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"friend", "reject"}, FriendRejectCmd, "<user_id> [zone_id]", "拒绝好友申请", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"friend", "remove"}, FriendRemoveCmd, "<user_id> [zone_id]", "删除好友", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"friend", "send_gift"}, FriendSendGiftCmd, "<user_id> <gift_type_id> [zone_id]", "发送好友礼物", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"friend", "receive_gift"}, FriendReceiveGiftCmd, "<gift_id> [gift_id...]", "领取好友礼物", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"friend", "get_suggest"}, FriendGetSuggestCmd, "", "获取推荐好友", nil, cmdDefaultTimeout)
}

func runFriendCommand(action base.TaskActionImpl, user user_data.User, name string,
	send func(*user_data.TaskActionUser) (int32, error)) error {
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		code, err := send(t)
		if err != nil {
			return err
		}
		if code < 0 {
			return fmt.Errorf("friend %s failed, errCode: %d", name, code)
		}
		return nil
	}, "Friend "+name+" Task"))
}

func friendUserKey(user user_data.User, args []string) (*public_protocol_pbdesc.DUserIDKey, error) {
	if len(args) < 1 || len(args) > 2 {
		return nil, fmt.Errorf("need <user_id> [zone_id]")
	}
	userID, err := strconv.ParseUint(args[0], 10, 64)
	if err != nil || userID == 0 {
		return nil, fmt.Errorf("invalid user_id %q", args[0])
	}
	var zoneID uint32
	if len(args) == 2 {
		v, parseErr := strconv.ParseUint(args[1], 10, 32)
		if parseErr != nil {
			return nil, fmt.Errorf("invalid zone_id %q", args[1])
		}
		zoneID = uint32(v)
	}
	return protocol.BuildUserKey(user, userID, zoneID), nil
}

func friendUserCommand(action base.TaskActionImpl, user user_data.User, args []string, name string,
	send func(*user_data.TaskActionUser, *public_protocol_pbdesc.DUserIDKey) (int32, error)) error {
	key, err := friendUserKey(user, args)
	if err != nil {
		return err
	}
	return runFriendCommand(action, user, name, func(t *user_data.TaskActionUser) (int32, error) {
		return send(t, key)
	})
}

func FriendGetAllCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	if len(args) != 0 {
		return fmt.Errorf("friend get_all takes no arguments")
	}
	return runFriendCommand(action, user, "get_all", func(t *user_data.TaskActionUser) (int32, error) {
		code, holder, err := lobbysvr_rpc_handle.SendFriendGetAll(t, user, &public_protocol_pbdesc.CSFriendGetAllReq{}, true)
		if err != nil || code < 0 {
			return code, err
		}
		rsp, err := holder.GetMessage()
		if err != nil {
			return code, err
		}
		t.Log("[friend] friends=%d inviters=%d invitees=%d gifts=%d sent=%d received=%d",
			len(rsp.GetFriendList()), len(rsp.GetInviterList()), len(rsp.GetInviteeList()), len(rsp.GetGiftList()),
			len(rsp.GetDailySendList()), len(rsp.GetDailyReceiveList()))
		for _, item := range rsp.GetFriendList() {
			t.Log("[friend] friend=%d:%d", item.GetUserKey().GetZoneId(), item.GetUserKey().GetUserId())
		}
		for _, item := range rsp.GetInviterList() {
			t.Log("[friend] inviter=%d:%d", item.GetFromUser().GetZoneId(), item.GetFromUser().GetUserId())
		}
		for _, item := range rsp.GetInviteeList() {
			t.Log("[friend] invitee=%d:%d", item.GetToUser().GetZoneId(), item.GetToUser().GetUserId())
		}
		for _, item := range rsp.GetGiftList() {
			t.Log("[friend] gift_id=%d from=%d:%d type=%d", item.GetGiftId(),
				item.GetFromUser().GetZoneId(), item.GetFromUser().GetUserId(), item.GetGiftTypeId())
		}
		return code, nil
	})
}

func FriendInviteCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	return friendUserCommand(action, user, args, "invite", func(t *user_data.TaskActionUser, key *public_protocol_pbdesc.DUserIDKey) (int32, error) {
		code, _, err := lobbysvr_rpc_handle.SendFriendInvite(t, user, &public_protocol_pbdesc.CSFriendInvitationReq{UserKey: key}, true)
		return code, err
	})
}

func FriendAcceptCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	return friendUserCommand(action, user, args, "accept", func(t *user_data.TaskActionUser, key *public_protocol_pbdesc.DUserIDKey) (int32, error) {
		code, _, err := lobbysvr_rpc_handle.SendFriendAcceptInvite(t, user, &public_protocol_pbdesc.CSFriendAcceptInvitationReq{UserKey: key}, true)
		return code, err
	})
}

func FriendRejectCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	return friendUserCommand(action, user, args, "reject", func(t *user_data.TaskActionUser, key *public_protocol_pbdesc.DUserIDKey) (int32, error) {
		code, _, err := lobbysvr_rpc_handle.SendFriendRejectInvite(t, user, &public_protocol_pbdesc.CSFriendRejectInvitationReq{UserKey: key}, true)
		return code, err
	})
}

func FriendRemoveCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	return friendUserCommand(action, user, args, "remove", func(t *user_data.TaskActionUser, key *public_protocol_pbdesc.DUserIDKey) (int32, error) {
		code, _, err := lobbysvr_rpc_handle.SendFriendRemove(t, user, &public_protocol_pbdesc.CSFriendRemoveReq{UserKey: key}, true)
		return code, err
	})
}

func FriendSendGiftCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	if len(args) < 2 || len(args) > 3 {
		return fmt.Errorf("need <user_id> <gift_type_id> [zone_id]")
	}
	keyArgs := args[:1]
	if len(args) == 3 {
		keyArgs = []string{args[0], args[2]}
	}
	key, err := friendUserKey(user, keyArgs)
	if err != nil {
		return err
	}
	giftTypeID, err := strconv.ParseInt(args[1], 10, 32)
	if err != nil || giftTypeID <= 0 {
		return fmt.Errorf("invalid gift_type_id %q", args[1])
	}
	return runFriendCommand(action, user, "send_gift", func(t *user_data.TaskActionUser) (int32, error) {
		code, _, err := lobbysvr_rpc_handle.SendFriendSendGift(t, user,
			&public_protocol_pbdesc.CSFriendSendGiftReq{UserKey: key, GiftTypeId: int32(giftTypeID)}, true)
		return code, err
	})
}

func FriendReceiveGiftCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	if len(args) == 0 {
		return fmt.Errorf("need <gift_id> [gift_id...]")
	}
	ids := make([]int64, 0, len(args))
	for _, arg := range args {
		id, err := strconv.ParseInt(arg, 10, 64)
		if err != nil || id <= 0 {
			return fmt.Errorf("invalid gift_id %q", arg)
		}
		ids = append(ids, id)
	}
	return runFriendCommand(action, user, "receive_gift", func(t *user_data.TaskActionUser) (int32, error) {
		code, holder, err := lobbysvr_rpc_handle.SendFriendReceiveGift(t, user,
			&public_protocol_pbdesc.CSFriendReceiveGiftReq{GiftIds: ids}, true)
		if err != nil || code < 0 {
			return code, err
		}
		rsp, err := holder.GetMessage()
		if err != nil {
			return code, err
		}
		for _, gift := range rsp.GetReceiveGifts() {
			t.Log("[friend] received gift_id=%d from=%d:%d", gift.GetGiftId(),
				gift.GetFromUser().GetZoneId(), gift.GetFromUser().GetUserId())
		}
		return code, nil
	})
}

func FriendGetSuggestCmd(action base.TaskActionImpl, user user_data.User, args []string) error {
	if len(args) != 0 {
		return fmt.Errorf("friend get_suggest takes no arguments")
	}
	return runFriendCommand(action, user, "get_suggest", func(t *user_data.TaskActionUser) (int32, error) {
		code, holder, err := lobbysvr_rpc_handle.SendFriendGetSuggest(t, user, &public_protocol_pbdesc.CSFriendGetSuggestReq{}, true)
		if err != nil || code < 0 {
			return code, err
		}
		rsp, err := holder.GetMessage()
		if err != nil {
			return code, err
		}
		t.Log("[friend] suggestions=%d", len(rsp.GetSuggestUsers()))
		for _, item := range rsp.GetSuggestUsers() {
			t.Log("[friend] suggested=%d:%d", item.GetUserKey().GetZoneId(), item.GetUserKey().GetUserId())
		}
		return code, nil
	})
}
