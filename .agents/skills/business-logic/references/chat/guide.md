# Chat Guide

Use for Lobby chat, team chat/notifications, DTMQ channel policy, and chat compliance integration. Public behavior is
documented in `doc/docs/components/chat.md`.

## Authoritative sources

- `src/lobbysvr/service/logic/chat/user_chat_manager.cpp` and `task_action_chat_send_message.cpp`: channel mapping,
  snapshots, heartbeats, sends, and compliance fallback.
- `src/server_frame/protocol/public/protocol/pbdesc/com.protocol.chat.proto` and `com.struct.chat.proto`: CS sync and
  supported channel keys.
- `src/teamsvr/sdk/common/rpc/team/team_common_api.cpp`: team channel keys.
- `src/lobbysvr/service/logic/team/user_team.cpp`: team cache/channel lifecycle and CS notification mapping.
- `resource/ExcelTables/DtMq.xlsx`: channel types, persistence, retention, and limits.

## Review checks

- Keep the channel matrix explicit. `memory_only=NO` channels persist through DTMQ; team and Orbit channels are
  memory-only. World currently accepts only partition 0.
- Snapshot retrieval enables active push. Heartbeats use sequence/hash to select incremental data or snapshot repair;
  test missing data, duplicates, cleanup, and repeated repair at the actual delivery boundary.
- Team chat carries both `DChatUserMessage` and `DTeamRoomNotification`. Verify `SCUserTeamRoomChgSync` content and
  delivery, plus channel removal when the team cache becomes invalid or loses its last member.
- Chat intentionally fails open when compliance RPC fails or returns no result. Empty/all-replacement filtered text
  is rejected; other matches send filtered text. Do not apply this policy implicitly to other compliance consumers.
- Orbit chat type 102 and battle-related declarations are not proof of end-to-end chat. Orbit rooms currently
  subscribe to event channel 101, and CS channel keys support only world/private/team. Verify actual subscription,
  send, and CS sync paths before documenting Orbit or battle chat as available.
- Changes to channel configuration require regenerating and inspecting DTMQ resources; update both chat and DTMQ
  documentation when public channel or persistence behavior changes.
