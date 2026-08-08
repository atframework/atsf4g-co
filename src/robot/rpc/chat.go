package atsf4g_go_robot_rpc

import (
	"fmt"

	pu "github.com/atframework/atframe-utils-go/proto_utility"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	user_data "github.com/atframework/robot-go/data"
)

// ChatGetAllChannelRpc 拉取当前用户所有订阅频道的元数据(含 channel_id 和 channel_type)。
// 返回的 channel_metadata 可用于后续 get_channel_snapshot / channel_heartbeat。
func ChatGetAllChannelRpc(action base.TaskActionImpl, user user_data.User) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCChatGetAllChannelRsp], error) {
	csBody := &public_protocol_pbdesc.CSChatGetAllChannelReq{}
	return lobbysvr_rpc_handle.SendChatGetAllChannel(action, user, csBody, true)
}

// ChatGetChannelSnapshotRpc 拉取指定频道的快照。首次拉取后服务端才会开始向该客户端推送频道通知。
func ChatGetChannelSnapshotRpc(action base.TaskActionImpl, user user_data.User, channelId string) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCChatGetChannelSnapshotRsp], error) {
	if channelId == "" {
		return 0, nil, fmt.Errorf("channel_id is empty")
	}
	csBody := &public_protocol_pbdesc.CSChatGetChannelSnapshotReq{
		ChannelId: channelId,
	}
	return lobbysvr_rpc_handle.SendChatGetChannelSnapshot(action, user, csBody, true)
}

// ChatChannelHeartbeatRpc 上报频道的同步点(last_sequence/last_hash_code)，服务端会返回增量消息或快照。
func ChatChannelHeartbeatRpc(action base.TaskActionImpl, user user_data.User,
	syncPoints []*public_protocol_pbdesc.DChannelSyncPoint) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCChatChannelHeartbeatRsp], error) {
	if len(syncPoints) == 0 {
		return 0, nil, fmt.Errorf("no sync points")
	}
	csBody := &public_protocol_pbdesc.CSChatChannelHeartbeatReq{
		HeartbeatData: syncPoints,
	}
	return lobbysvr_rpc_handle.SendChatChannelHeartbeat(action, user, csBody, true)
}

// ChatSendTextMessageRpc 向世界频道(partition_id=0)或私聊频道发送文本消息。
//   - worldPartition=0 表示世界频道(服务端按自身 world_id 解析)
//   - privateUserId>0 表示私聊目标用户(使用当前用户的 zone_id)
func ChatSendTextMessageRpc(action base.TaskActionImpl, user user_data.User, text string,
	worldPartition uint64, privateUserId uint64) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCChatSendMessageRsp], error) {
	if text == "" {
		return 0, nil, fmt.Errorf("text is empty")
	}
	csBody := &public_protocol_pbdesc.CSChatSendMessageReq{
		ChannelKey: buildChatChannelKey(user, worldPartition, privateUserId),
		Detail: &public_protocol_pbdesc.DChannelMessageDetail{
			Command: &public_protocol_pbdesc.DChannelMessageDetail_Text{
				Text: text,
			},
		},
	}
	return lobbysvr_rpc_handle.SendChatSendMessage(action, user, csBody, true)
}

// buildChatChannelKey 构造 CSChatSendMessageReq 使用的频道 Key。
//   - privateUserId>0: 私聊频道，使用当前用户 zone_id + 目标 user_id
//   - 否则: 世界分片频道(partition_id=worldPartition，服务端补 world_id)
func buildChatChannelKey(user user_data.User, worldPartition uint64, privateUserId uint64) *public_protocol_pbdesc.DChatChannelKey {
	if privateUserId > 0 {
		return &public_protocol_pbdesc.DChatChannelKey{
			KeyType: &public_protocol_pbdesc.DChatChannelKey_PrivateChannel{
				PrivateChannel: &public_protocol_pbdesc.DUserIDKey{
					UserId: privateUserId,
					ZoneId: user.GetZoneId(),
				},
			},
		}
	}
	return &public_protocol_pbdesc.DChatChannelKey{
		KeyType: &public_protocol_pbdesc.DChatChannelKey_WorldPartitionChannel{
			WorldPartitionChannel: worldPartition,
		},
	}
}

// RegisterChatChannelSyncPushHandler 注册服务端推送的 SCChatChannelSync 处理器。
// 收到推送时打印每个频道的增量消息或快照内容。
func RegisterChatChannelSyncPushHandler(user user_data.User) {
	lobbysvr_rpc_handle.RegisterMessageHandlerChatChannelSync(user, func(action *user_data.TaskActionUser, msg *public_protocol_pbdesc.SCChatChannelSync, errCode int32) error {
		if errCode < 0 {
			action.User.Log("[chat] SCChatChannelSync push error, errCode=%d", errCode)
			return nil
		}
		for _, ch := range msg.GetChatChannel() {
			meta := ch.GetMetadata()
			channelId := ""
			channelType := uint32(0)
			if meta != nil && meta.GetChannelKey() != nil {
				channelId = meta.GetChannelKey().GetChannelId()
				channelType = meta.GetChannelKey().GetChannelType()
			}
			if inc := ch.GetIncremental(); inc != nil {
				for _, m := range inc.GetMessageList() {
					action.User.Log("[chat] push incremental channel=%s(type=%d): sequence=%d, sender=%s, text=%s",
						channelId, channelType, m.GetSequence(), m.GetSenderKey(), m.GetDetail().GetText())
				}
			}
			if snap := ch.GetSnapshot(); snap != nil {
				action.User.Log("[chat] push snapshot channel=%s(type=%d), message_count=%d",
					channelId, channelType, len(snap.GetMessageList()))
				for _, m := range snap.GetMessageList() {
					action.User.Log("[chat] push snapshot msg: sequence=%d, sender=%s, text=%s",
						m.GetSequence(), m.GetSenderKey(), m.GetDetail().GetText())
				}
			}
		}
		return nil
	})
}
