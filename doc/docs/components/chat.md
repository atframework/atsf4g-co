---
title: 聊天
---

# 聊天

聊天由 `lobbysvr` 的 `service/logic/chat/` 和 DTMQ 提供，不存在独立的 `chatsvr`。世界、私聊和队伍频道
共用 CS 快照、心跳与增量同步协议；队伍频道还承载队伍系统通知。

## 频道与持久化

频道类型和存储策略由 `resource/ExcelTables/DtMq.xlsx` 的 `dtmq_channel_type` 配置：

| 频道 | type | `memory_only` | 当前用途 |
| --- | ---: | --- | --- |
| 世界 | 1 | `NO` | 世界聊天；当前只使用分片 0 |
| 私聊 | 2 | `NO` | 玩家私聊 |
| 系统通知 | 3 | `NO` | 生命周期较短的系统消息 |
| 系统公告 | 4 | `NO` | 生命周期较长的系统消息 |
| 队伍聊天 | 12 | `YES` | 队伍文本和队伍系统通知 |
| Orbit 事件 | 101 | `YES` | `DOrbitRoomEventLog` 房间事件 |
| Orbit 聊天 | 102 | `YES` | Orbit 房间的战场/阵营聊天，创建房间时按配置订阅 |

`memory_only=NO` 的频道由 DTMQ 持久化记录；`YES` 的频道跳过 DB 保存。世界、私聊和队伍消息最终都经
`user_chat_manager::send_event_message` 写入 DTMQ。客户端发送非零 `world_partition_channel` 会被拒绝。

## CS 同步流程

1. `chat_get_all_channel` 只返回频道元数据。
2. 对每个目标频道调用 `chat_get_channel_snapshot`；成功拉取初始快照后才开启该频道的主动推送。
3. `chat_channel_heartbeat` 携带各频道同步点。Lobby 按 sequence/hash 选择增量消息或快照修复，
   通过 `SCChatChannelSync` 下发。
4. `chat_send_message` 使用 `DChatChannelKey` 选择 world/private/team key，并用 `DChatMessageContent`
   提交内容。

推送速率和缓冲分别由 `lobbysvr_chat_cfg.push_message_per_second` 与
`lobbysvr_chat_cfg.push_message_buffer_max_count` 控制。

## 队伍频道

`team_common_api.cpp` 使用 `(zone_id, team_id)` 生成队伍聊天频道 key。频道随 Lobby 队伍缓存建立，
在队伍失效或最后成员退出时移除。

频道内有两类业务消息：

- 普通聊天：`atframework.chat.DChatUserMessage`。
- 队伍通知：`DTeamRoomNotification`，覆盖队长变更、准备、开始匹配、匹配结果和战斗就绪等事件。

Lobby 将 DTMQ 队伍事件映射为 `SCUserTeamRoomChgSync`。客户端不能只处理文本消息而忽略队伍通知。

## 文本合规策略

发送文本前调用[合规服务](compliance)。当前聊天策略为：

- 合规 RPC 失败或没有结果：记录警告并发送原文。
- 命中且过滤文本为空，或过滤后只剩替换字符：返回 `EN_ERR_TEXT_HAS_DIRTY_WORD`，不发送。
- 其他命中：发送替换后的文本。

这是聊天模块的 fail-open 选择，不是所有合规调用方的通用策略。

## Orbit 与战斗频道边界

Orbit 房间创建时（`user_orbit_manager::create_room`）除订阅事件频道 `EN_ORBIT_CHANNEL_TYPE_ROOM=101` 外，
还会按配置订阅 `EN_ORBIT_CHANNEL_TYPE_CHAT=102` 的聊天频道：`lobbysvr_cfg.orbit.allow_battlefield_chat_channel`
控制战场频道 `<room client_id>:battlefield`，`lobbysvr_cfg.orbit.allow_faction_chat_channel` 控制阵营频道
`<room client_id>:faction:<faction_id>`。

订阅的聊天频道注册进 `user_chat_manager`，复用统一的 CS 快照、心跳与增量同步；频道元数据随
`DOrbitRoomUserData.chat_channel` 下发。

发送侧尚未接入：CS `DChatChannelKey` 仍只有 world/private/team 三种 key，
`user_chat_manager::build_dtmq_channel_key_from_chat_channel_key` 对其他 key 返回
`EN_ERR_DTMQ_INVALID_CHANNEL`，客户端还不能通过聊天 RPC 向 Orbit 频道发送消息。

实现入口为 `src/lobbysvr/service/logic/chat/`、`src/teamsvr/sdk/common/rpc/team/`、
`src/orbitsvr/service/logic/room/` 和[DTMQ](dtmq)。
