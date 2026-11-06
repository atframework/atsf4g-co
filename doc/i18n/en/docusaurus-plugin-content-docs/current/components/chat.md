---
title: Chat
---

# Chat

Chat is provided by `lobbysvr`'s `service/logic/chat/` and DTMQ; there is no separate `chatsvr`. World, private,
and team channels share CS snapshot, heartbeat, and incremental-sync protocols. The team channel also carries team
system notifications.

## Channels and Persistence

`dtmq_channel_type` in `resource/ExcelTables/DtMq.xlsx` defines channel types and storage policies:

| Channel | type | `memory_only` | Current use |
| --- | ---: | --- | --- |
| World | 1 | `NO` | World chat; currently only partition 0 is used |
| Private | 2 | `NO` | Player private messages |
| System notification | 3 | `NO` | Short-lived system messages |
| System announcement | 4 | `NO` | Longer-lived system messages |
| Team chat | 12 | `YES` | Team text and team system notifications |
| Orbit events | 101 | `YES` | `DOrbitRoomEventLog` room events |
| Orbit chat | 102 | `YES` | Channel declared; end-to-end integration is not complete |

DTMQ persists records for channels with `memory_only=NO` and skips DB saves for `YES`. World, private, and team
messages ultimately reach DTMQ through `user_chat_manager::send_event_message`. A nonzero client-provided
`world_partition_channel` is rejected.

## CS Synchronization

1. `chat_get_all_channel` returns only channel metadata.
2. Call `chat_get_channel_snapshot` for each target channel; active push starts only after its initial snapshot.
3. `chat_channel_heartbeat` carries channel sync points. Lobby uses sequence/hash values to select incremental
   messages or snapshot repair and sends `SCChatChannelSync`.
4. `chat_send_message` selects a world/private/team key through `DChatChannelKey` and submits
   `DChatMessageContent`.

`lobbysvr_chat_cfg.push_message_per_second` and `lobbysvr_chat_cfg.push_message_buffer_max_count` control push rate
and buffering respectively.

## Team Channel

`team_common_api.cpp` builds the team chat channel key from `(zone_id, team_id)`. The channel is created with the
Lobby team cache and removed when the team becomes invalid or its last member leaves.

The channel carries two kinds of business messages:

- Ordinary chat: `atframework.chat.DChatUserMessage`.
- Team notifications: `DTeamRoomNotification`, including captain changes, readiness, matchmaking start/results,
  and battle-ready events.

Lobby maps DTMQ team events to `SCUserTeamRoomChgSync`. Clients must not handle text while ignoring team
notifications.

## Text Compliance Policy

Text sending calls the [compliance service](compliance) first. Current chat policy is:

- Compliance RPC failure or no result: log a warning and send the original text.
- A match with empty filtered text, or only replacement characters remaining: reject with
  `EN_ERR_TEXT_HAS_DIRTY_WORD`.
- Other matches: send the replaced text.

This fail-open choice belongs to chat; it is not a universal policy for all compliance callers.

## Orbit and Battle Channel Boundaries

`EN_ORBIT_CHANNEL_TYPE_CHAT=102` and the corresponding Excel configuration exist, but Orbit rooms currently
subscribe to the event channel `EN_ORBIT_CHANNEL_TYPE_ROOM=101`. CS `DChatChannelKey` still has only
world/private/team keys.

Orbit/battle-room chat is therefore a configuration or protocol reservation, not proof of complete room
subscription, message sending, and client synchronization.

Implementation entry points are `src/lobbysvr/service/logic/chat/`, `src/teamsvr/sdk/common/rpc/team/`,
`src/orbitsvr/service/logic/room/`, and [DTMQ](dtmq).
