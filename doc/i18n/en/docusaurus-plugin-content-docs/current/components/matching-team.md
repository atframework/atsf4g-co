---
title: Matchmaking and Teams
---

# Matchmaking and Teams

`src/matchsvr/` provides matchmaking pools, rooms, and result notifications;
`src/teamsvr/` provides team rooms; its `teamsvr-match` teammate-search service remains a placeholder.
The lobby's `logic/matching/` and `logic/team/` integrate player-facing behavior.

## Quick Start: Matchmaking

1. Enable a `matchsvr` instance and configure Redis. Edit pools, rule groups, matching rules, and parameter
   merge rules in `resource/ExcelTables/Matching.xlsx`; follow `Level.xlsx` for levels.
2. [Export and publish Excel configuration](../development/excel-config), and add `matchsvr-sdk` to the consumer's `USE_SERVICE_SDK`.
3. Use generated APIs in `src/matchsvr/sdk/rpc/matching/matchsvrservice.atfw.gen.h`.
   Follow the lobby's `user_matching_manager.h/.cpp` for player workflows, and register
   `MatchsvrNotifyService` handlers for results.
4. Use the existing `src/robot/case_config/matching_demo.conf` scenario to verify start, confirmation,
   cancellation, and result notifications. See `matching_team_3v3.conf` for team matching. Waiting-room cases are
   described in `matching_waiting_cases.md`, but their pools and levels are absent from the default workbooks;
   prepare the additional fixtures documented there instead of running them against default resources.

Existing matching parameters are configured through Excel and values. New algorithms or matching semantics
need business implementations. See [Orbit](orbit) for battle process management after matching.

### Waiting Rooms and State

The lobby persists `DUserMatchingData` in `table_user.matching_data`, including `level_data.level_select`.
`user_matching_manager::init_from_table_data` and `dump` restore and write this data respectively.

After initial Orbit user initialization succeeds, a waiting room admits late Units into the existing room without
recreating DS or remapping established factions. Admission is bounded by room and faction reservations and checks
the absolute `end_join_timepoint` at admission, confirmation, initialization, retry, and post-await commit. A late
success cannot revive a timed-out Unit. A late-unit initialization failure closes the whole waiting room without
rolling back Units that already succeeded. A successful player may match elsewhere with a new Unit ID, but the old
waiting room rejects both admitted Unit IDs and successful player identities.

Match's Orbit EndJoin/UserQuit actions currently return success as placeholders; they do not establish completed DS
settlement. See `src/matchsvr/README.md` for the complete state and WAL contract.

## Quick Start: Teams

1. Enable `teamsvr-room`; add `matchsvr` when teams enter battle matchmaking.
   Edit and export team types in `resource/ExcelTables/Team.xlsx`.
2. Add `team-common-sdk` and `team-sdk-room` to the consumer's `USE_SERVICE_SDK`.
   Room APIs live in `src/teamsvr/sdk/room/rpc/team/team_room_client_api.h`.
3. Follow `src/lobbysvr/service/logic/team/user_team_manager.h/.cpp` for members, invitations, captains, and state synchronization.
4. With test users, verify team creation, invitations/joining, leaving, and any matching workflow; inspect client state and notifications.

Invitations and active join requests are separate flows. Use `team_send_join_request` for a request and
`team_accept_join_request` / `team_reject_join_request` for review. `EN_TTDF_DISABLE_INVITATION` and
`EN_TTDF_DISABLE_APPLY` disable invitation and request flows respectively. Verify Lobby caching, upstream merge, and
required, deduplicated, and recovered full/incremental notifications at the actual flush boundary.

`TeamMatchService.search` in `teamsvr-match` has no teammate-search implementation. It is separate from entering
teams into `matchsvr` and is outside the minimal integration above.

## Customization and Design

Matchmaking protocols live under `src/matchsvr/protocol/`, team protocols under `src/teamsvr/protocol/`,
and client messages in `com.protocol.match.proto` and `com.protocol.team.proto`.
Algorithms and WAL implementations live in service `logic/` directories; tests live in their `test/` directories.
For new RPCs or config fields, use the [RPC](../development/add-rpc-task) and
[server config](../development/server-config) guides.
