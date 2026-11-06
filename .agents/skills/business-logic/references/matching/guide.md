# Matchmaking

Read this guide for matchmaking work. Preserve current contracts across configuration, Lobby ownership, `matchsvr`
state, WAL recovery, faction assignment, waiting rooms, Orbit handoff, and client-visible events. Use current source
instead of relying on removed result-template or view designs.

## Contents

- [Related guidance](#load-related-guidance)
- [System map](#system-map)
- [Review or change workflow](#review-or-change-workflow)
- [Test and validation requirements](#test-and-validation-requirements)
- [Review baseline maintenance](#review-baseline-maintenance)

## Load related guidance

- Read [engineering-guidelines](../../../engineering-guidelines/SKILL.md) before reviewing or editing C++, protobuf,
  CMake, or generated-code inputs.
- Read [change-workflow](../../../change-workflow/SKILL.md) before fixing a defect or changing matchmaking behavior
  across modules.
- Read [rpc-unit-test](../../../rpc-unit-test/SKILL.md) for offline mock runtime test work and
  [testing](../../../testing/SKILL.md) for executable filters and Windows DLL lookup.
- Read [build](../../../build/SKILL.md) before configuring or building. Resolve `<BUILD_DIR>` from workspace settings
  and put all scratch output under `<BUILD_DIR>/_agent_tmp/...`.

## System map

- `resource/ExcelTables/Matching.xlsx`: designer-facing pools, rules, waiting-room rules, and feature switches.
- `src/server_frame/protocol/public/protocol/config/com.struct.matching.config.proto`: configuration schema.
- `src/server_frame/config/include/config/excel_config_matching_index.h` and
  `src/server_frame/config/src/excel_config_matching_index.cpp`: configuration indexes.
- `src/server_frame/protocol/public/protocol/pbdesc/com.{protocol,struct}.match.proto`: current Unit views, Lobby
  state, requests, events, scopes, and acknowledgement cursors.
- `src/lobbysvr/service/logic/matching/`: start/check/confirm/cancel, level selection persistence, and local ownership.
- `src/lobbysvr/service/logic/action/task_action_matching_event_sync.cpp`: Match-to-Lobby event application.
- `src/matchsvr/service/logic/matching/`: buckets, rooms, rules, factions, waiting rooms, lifecycle, and WAL.
- `src/matchsvr/service/logic/action/`: inbound matching RPCs and Orbit lifecycle callbacks.
- `src/matchsvr/README.md`: current waiting-room and Orbit handoff contract.
- `src/matchsvr/test/`: room, rule, lifecycle, migration, and integration-style offline tests.

## Review or change workflow

1. Trace one complete request before editing:
   `Lobby start -> matching SDK/RPC -> manager bucket -> room -> confirm -> Orbit create -> room-ready -> event sync`.
   Record ownership, state, timeout, acknowledgement, and error propagation at every boundary.
2. Keep every Unit atomic across factions, migration, waiting rooms, and battle initialization. Allocate a new Unit ID
   for a new matching attempt, reject stale destructive requests, and keep unfinished Units from duplicate admission.
3. Treat Excel keys and protobuf fields as one contract. Compare workbook key rows with protobuf fields, regenerate
   resources, and inspect generated matching JSON. A successful generator exit does not prove columns were consumed.
4. Preserve the scope/level model:
   - `DMatchingScope` is the coarse bucket: `level_type`, region, battle version, and pool.
   - Every Unit carries normalized `acceptable_level_ids`; all Units in a room need a non-empty intersection.
   - The room owns `selected_level_id`, keeps it inside the current intersection, and Orbit consumes that selection.
   Validate client selection against server-owned configuration. Lobby persists level selection in
   `DUserMatchingData.level_data`; do not add a second divergent owner.
5. Read current configuration before changing faction readiness or placement. `allow_incomplete_factions` and current
   capacity/readiness fields are authoritative; do not reintroduce removed result-template/capacity-multiset rules.
   Check fixed faction capacity, fill policy, cancellation, and rollback with current tests.
6. For WAL and subscriber changes, verify the actual event order in current source and tests. Group delivery by the
   current Lobby server and Unit ownership, filter events to the subscribed Unit, and prevent late callbacks from
   advancing a newer Unit. Do not infer an ordering contract from an obsolete design description.
7. Keep acknowledgement state explicit. Current `SSMatchingCheckReq` carries `acknowledge_event_id`; confirmation and
   heartbeat paths use the current Unit event IDs and Lobby ACK state. Old requests must not commit a newer event.
8. For waiting-room work, preserve `src/matchsvr/README.md`:
   - Late Units initialize into the existing Orbit room without recreating DS or remapping established factions.
     Reservations count against room and fixed-faction capacities; full reservation alone does not permanently close
     the room.
   - `end_join_timepoint` is absolute. Require `now < end_join_timepoint` at admission, confirmation, initialization,
     retry, and post-await commit. Late success must not revive a Unit or emit another `BATTLE_READY`.
   - A late-unit initialization failure closes the whole waiting room and ends pending Units without rolling back
     successful Units. Successful players may match elsewhere with a new Unit ID, but the old waiting room rejects
     admitted Unit IDs and successful player identities.
9. Match EndJoin/UserQuit actions currently return success as placeholders. Do not describe Match local completion as
   completed DS settlement. Load the Orbit guide when changing this boundary.
10. Keep internal, Lobby, and CS views aligned with current protobuf definitions, including `DMatchingUnitView` and
    `DUserMatchingData`. Do not pass internal WAL cursors or full-room diagnostics through client messages.

## Test and validation requirements

- Assert `runtime.start(options) == 0`; a failed fixture must not fall through to a passing result.
- Exercise real actions through `atframework::testing::invoke_ss_action<TAction>` when targeting a known SS action.
  Set source metadata explicitly for forwarding and migration behavior.
- Cover invalid configuration, duplicate requests, confirmation timeout, battle-create failure, room-ready failure,
  migration ordering, late/duplicate events, and ACK isolation.
- For waiting rooms, cover post-initialization failure closing the room, pending-Unit termination, reservation
  release, exact/late deadlines, successful-player rematching, and rejection from the previous waiting room.
- Discover exact targets from `src/matchsvr/test/CMakeLists.txt`, run focused cases and the full executable, and
  inspect fixture setup and skipped prerequisites rather than relying only on exit code.
- Regenerate matching resources after schema/workbook changes and assert required fields in generated output. The
  default workbook does not contain every robot waiting-room fixture; check each scenario's resource prerequisites.
- Finish with checks routed by engineering guidelines and `git diff --check`. Do not rebuild for Skill-only edits.

## Review baseline maintenance

- Read [the dated review baseline](review-baseline.md) only for relevant reviews or fixes. Reverify every item against
  current source before reporting it as open; remove or update stale baseline items after a fix lands.
- Distinguish reproduced defects from scalability or availability risks. Never present a baseline observation as a
  current fact without checking current code and generated output.
