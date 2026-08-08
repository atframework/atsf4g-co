package atsf4g_go_robot_task

import (
	"fmt"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"
)

// ChatGetAllChannelTask 拉取当前用户所有订阅频道的元数据并打印。
// 返回频道列表(channel_id/channel_type/last_message_sequence 等)。
func ChatGetAllChannelTask(task *user_data.TaskActionUser) ([]*public_protocol_pbdesc.DChatChannelMeta, error) {
	errCode, rspHolder, rpcErr := protocol.ChatGetAllChannelRpc(task, task.User)
	if rpcErr != nil {
		return nil, rpcErr
	}
	if errCode < 0 {
		return nil, fmt.Errorf("chat_get_all_channel failed, errCode: %d", errCode)
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return nil, fmt.Errorf("failed to get chat_get_all_channel response message: %v", err)
	}
	return rsp.GetChannelMetadata(), nil
}

// ChatGetChannelSnapshotTask 拉取指定频道的快照。首次拉取后服务端才会开始向该客户端推送频道通知。
func ChatGetChannelSnapshotTask(task *user_data.TaskActionUser, channelId string) error {
	errCode, rspHolder, rpcErr := protocol.ChatGetChannelSnapshotRpc(task, task.User, channelId)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("chat_get_channel_snapshot failed, errCode: %d", errCode)
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get chat_get_channel_snapshot response message: %v", err)
	}

	snapshot := rsp.GetChannelSnapshot()
	if snapshot == nil {
		return nil
	}
	meta := snapshot.GetMetadata()
	if meta != nil {
		task.User.Log("[chat] snapshot channel_id=%s, type=%d, last_message_sequence=%d",
			meta.GetChannelKey().GetChannelId(), meta.GetChannelKey().GetChannelType(), meta.GetLastMessageSequence())
	}
	for _, msg := range snapshot.GetSnapshot().GetMessageList() {
		task.User.Log("[chat] snapshot msg: sequence=%d, sender=%s, text=%s",
			msg.GetSequence(), msg.GetSenderKey(), msg.GetDetail().GetText())
	}
	return nil
}

// ChatChannelHeartbeatTask 上报频道的同步点，服务端返回增量消息或快照(通过推送 SCChatChannelSync)。
// syncPoints 由调用方根据本地缓存的 last_sequence/last_hash_code 构造。
func ChatChannelHeartbeatTask(task *user_data.TaskActionUser, syncPoints []*public_protocol_pbdesc.DChannelSyncPoint) error {
	errCode, _, rpcErr := protocol.ChatChannelHeartbeatRpc(task, task.User, syncPoints)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("chat_channel_heartbeat failed, errCode: %d", errCode)
	}
	return nil
}

// ChatSendTextMessageTask 向世界频道(worldPartition=0)或私聊频道(privateUserId>0)发送文本消息。
func ChatSendTextMessageTask(task *user_data.TaskActionUser, text string, worldPartition uint64, privateUserId uint64) error {
	errCode, _, rpcErr := protocol.ChatSendTextMessageRpc(task, task.User, text, worldPartition, privateUserId)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("chat_send_message failed, errCode: %d", errCode)
	}
	return nil
}
