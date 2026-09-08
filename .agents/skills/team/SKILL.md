---
name: team
description: "Use when: developing, diagnosing, or reviewing Lobby team integration, Team Room membership or admission, CS team dirty notifications, or team repair tests. Do not use for matchmaking algorithms or generic DTMQ infrastructure."
---

# Team Integration

## Establish the contract

- Read the affected paths in [Lobby team](../../../src/lobbysvr/service/logic/team/) and
  [Team Room](../../../src/teamsvr/service/room/logic/room/team_room.cpp), including the caller, callback, and error producer.
  For an overall review, include every team task handler and the manager's lifecycle paths.
- For CS changes, compare recent protocol commits with
  [team data](../../../src/server_frame/protocol/public/protocol/pbdesc/com.struct.team.proto),
  [team RPCs](../../../src/server_frame/protocol/public/protocol/pbdesc/com.protocol.team.proto), and
  [user RPCs](../../../src/server_frame/protocol/public/protocol/pbdesc/com.protocol.user.proto).
  For Room RPC changes, also read
  [the Room service schema](../../../src/teamsvr/protocol/room/protocol/pbdesc/team_room_service.proto).
- Keep acceptance and case mappings in
  [USER_TEAM_TEST_PLAN.md](../../../src/lobbysvr/service/logic/team/USER_TEAM_TEST_PLAN.md).
  Recheck current source before reusing historical test results or protocol descriptions.
- Use [engineering-guidelines](../engineering-guidelines/SKILL.md) for C++/protobuf changes and
  [rpc-unit-test](../rpc-unit-test/SKILL.md) for runtime fixtures and RPC validation.

## Trace delivery and repair

- Follow changes from Room actions or personal-channel messages through cache updates, dirty registration, and the
  actual CS response, channel batch, snapshot, or chat flush. Check registration and delivery separately.
- Preserve the first-pull gate. `SCUserGetInfoRsp.user_team` replaces the full `DTeamUserData`, including an explicitly
  empty state. Account for dirty records consumed by that replacement before checking subsequent pushes.
- Distinguish initial publication, actual snapshot recovery, membership reactivation, and ordinary actions. Empty
  actions, duplicate membership events, and metadata updates must not manufacture snapshots or empty dirty pushes.
  A known member replayed after a snapshot must not send initial member defaults back to Room.
- Check both team-level admission caches and the player's pending invitation/join-request lists. Expiry and invalid
  pending RPC results must register the matching removal without adding an independent flush.
- Verify error meanings in Room: operator membership loss can repair local membership; a missing target member,
  permission denial, or transport failure does not establish that the operator left the team.
- Check active object identity after awaited RPCs. A late callback from a replaced object must not remove the current
  team. Check removal deduplication across get-info, exit, kick, destroy, cleanup, and authoritative reentry.

## Verify at observable boundaries

- Use [the shared fixture](../../../src/lobbysvr/test/lobbysvr_test_user_team_common.h) and the existing test targets.
  Preserve the real dispatcher, subscriber callback order, and CS pre-refresh path; do not replace them with invented
  production hooks. Use direct manager tests when pre-refresh would hide the boundary being tested.
- Assert raw notification counts as well as decoded entries. Cover one normal action, multiple actions in one batch,
  no action, repeated cleanup, remove followed by reentry, and repair with pending removals. Use controlled time and
  expected outbound RPCs to detect unsolicited state writes during recovery.
- Build and run the affected Lobby and Team Room targets using the selected workspace build tree and CTest environment.
  Record actual case counts, platform/configuration, and unrun checks in the test plan. A discovered or empty suite
  is not executed coverage.
