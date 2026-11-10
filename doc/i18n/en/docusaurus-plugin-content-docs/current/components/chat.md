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
| Orbit chat | 102 | `YES` | Battlefield/faction chat of an Orbit room; subscribed per config at room creation |

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

When an Orbit room is created (`user_orbit_manager::create_room`), lobbysvr subscribes to
`EN_ORBIT_CHANNEL_TYPE_CHAT=102` chat channels in addition to the event channel `EN_ORBIT_CHANNEL_TYPE_ROOM=101`:
`lobbysvr_cfg.orbit.allow_battlefield_chat_channel` controls the battlefield channel `<room client_id>:battlefield`,
and `lobbysvr_cfg.orbit.allow_faction_chat_channel` controls the faction channel
`<room client_id>:faction:<faction_id>`.

The subscribed chat channels are registered into `user_chat_manager` and reuse the unified CS snapshot, heartbeat,
and incremental sync; channel metadata is delivered with `DOrbitRoomUserData.chat_channel`.

The send side is not integrated yet: CS `DChatChannelKey` still has only world/private/team keys, and
`user_chat_manager::build_dtmq_channel_key_from_chat_channel_key` returns `EN_ERR_DTMQ_INVALID_CHANNEL` for other
keys, so clients cannot send to Orbit channels through the chat RPC yet.

Implementation entry points are `src/lobbysvr/service/logic/chat/`, `src/teamsvr/sdk/common/rpc/team/`,
`src/orbitsvr/service/logic/room/`, and [DTMQ](dtmq).
