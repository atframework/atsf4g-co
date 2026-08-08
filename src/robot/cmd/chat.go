package atsf4g_go_robot_cmd

import (
	"fmt"
	"strconv"
	"strings"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	public_protocol_common "github.com/atframework/atsf4g-co/component/public/protocol/common"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	robot_cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
)

// ========================= 注册指令 =========================
func init() {
	robot_cmd.RegisterUserCommand([]string{"chat", "get_all_channel"}, ChatGetAllChannelCmd, "",
		"List all subscribed chat channels (returns channel_id list)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"chat", "get_snapshot"}, ChatGetSnapshotCmd, "<channel_id>",
		"Fetch snapshot of a channel (push starts after first fetch)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"chat", "heartbeat"}, ChatHeartbeatCmd,
		"<channel_id> [last_sequence] [last_hash_code]",
		"Channel heartbeat / incremental sync", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"chat", "send"}, ChatSendCmd,
		"<world|private> <content|user_id> [content]",
		"Send a chat message (chat send world <text> | chat send private <user_id> <text>)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"chat", "register_push"}, ChatRegisterPushCmd, "",
		"Register/re-register chat push handler (prints on SCChatChannelSync)", nil, cmdDefaultTimeout)
}

// ChatGetAllChannelCmd 获取所有订阅频道元数据。
func ChatGetAllChannelCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(act *user_data.TaskActionUser) error {
		metas, err := task.ChatGetAllChannelTask(act)
		if err != nil {
			return err
		}
		if len(metas) == 0 {
			act.User.Log("[chat] no channel subscribed")
			return nil
		}
		for _, meta := range metas {
			key := meta.GetChannelKey()
			act.User.Log("[chat] channel_id=%s, type=%d, last_message_sequence=%d, last_removed_sequence=%d",
				key.GetChannelId(), key.GetChannelType(), meta.GetLastMessageSequence(), meta.GetLastRemovedSequence())
		}
		return nil
	}, "ChatGetAllChannel Task"))
}

// ChatGetSnapshotCmd 拉取指定频道的快照。
func ChatGetSnapshotCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 {
		return fmt.Errorf("usage: chat get_snapshot <channel_id>")
	}
	channelId := cmd[0]
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(act *user_data.TaskActionUser) error {
		return task.ChatGetChannelSnapshotTask(act, channelId)
	}, "ChatGetSnapshot Task"))
}

// ChatHeartbeatCmd 频道心跳/增量同步。
// 用法: chat heartbeat <channel_id> [last_sequence] [last_hash_code]
func ChatHeartbeatCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 {
		return fmt.Errorf("usage: chat heartbeat <channel_id> [last_sequence] [last_hash_code]")
	}
	channelId := cmd[0]
	var lastSequence int64
	var lastHashCode uint64
	if len(cmd) >= 2 {
		v, err := strconv.ParseInt(cmd[1], 10, 64)
		if err != nil {
			return fmt.Errorf("invalid last_sequence: %v", err)
		}
		lastSequence = v
	}
	if len(cmd) >= 3 {
		v, err := strconv.ParseUint(cmd[2], 10, 64)
		if err != nil {
			return fmt.Errorf("invalid last_hash_code: %v", err)
		}
		lastHashCode = v
	}

	syncPoint := &public_protocol_pbdesc.DChannelSyncPoint{
		ChannelKey: &public_protocol_common.DChannelIdKey{
			ChannelId: channelId,
		},
		LastSequence: lastSequence,
		LastHashCode: lastHashCode,
	}

	return action.AwaitTask(user.RunTaskDefaultTimeout(func(act *user_data.TaskActionUser) error {
		return task.ChatChannelHeartbeatTask(act, []*public_protocol_pbdesc.DChannelSyncPoint{syncPoint})
	}, "ChatHeartbeat Task"))
}

// ChatSendCmd 发送聊天消息。
// 用法:
//
//	chat send world <text...>
//	chat send private <user_id> <text...>
func ChatSendCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 2 {
		return fmt.Errorf("usage: chat send <world|private> <content...>  or  chat send private <user_id> <content...>")
	}
	mode := cmd[0]
	switch strings.ToLower(mode) {
	case "world":
		text := strings.Join(cmd[1:], " ")
		return action.AwaitTask(user.RunTaskDefaultTimeout(func(act *user_data.TaskActionUser) error {
			return task.ChatSendTextMessageTask(act, text, 0, 0)
		}, "ChatSendWorld Task"))
	case "private":
		if len(cmd) < 3 {
			return fmt.Errorf("usage: chat send private <user_id> <content...>")
		}
		targetUserId, err := strconv.ParseUint(cmd[1], 10, 64)
		if err != nil {
			return fmt.Errorf("invalid user_id: %v", err)
		}
		text := strings.Join(cmd[2:], " ")
		return action.AwaitTask(user.RunTaskDefaultTimeout(func(act *user_data.TaskActionUser) error {
			return task.ChatSendTextMessageTask(act, text, 0, targetUserId)
		}, "ChatSendPrivate Task"))
	default:
		return fmt.Errorf("unknown send mode: %s (use world|private)", mode)
	}
}

// ChatRegisterPushCmd 注册聊天频道推送处理器。
// 收到服务端推送的 SCChatChannelSync 时打印增量消息或快照内容。
func ChatRegisterPushCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	protocol.RegisterChatChannelSyncPushHandler(user)
	user.Log("[chat] SCChatChannelSync push handler registered")
	return nil
}
