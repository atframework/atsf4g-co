---
name: matching
description: "Use when: developing, diagnosing, or reviewing matchmaking across matchsvr/lobbysvr, matching pools and rules, faction assignment, matching WAL migration, Orbit battle handoff, Matching.xlsx generation, or matchsvr tests. Do not use when: changing generic lobby, Orbit, RPC, or configuration infrastructure unrelated to matchmaking."
---

# Matchmaking

Use this Skill to preserve end-to-end matchmaking invariants across configuration, lobby ownership, `matchsvr` state,
WAL migration, faction assignment, Orbit room creation, and client-visible events.

## Load related guidance

- Read `../engineering-guidelines/SKILL.md` before reviewing or editing C++, protobuf, CMake, or generated-code inputs.
- Read `../change-workflow/SKILL.md` before fixing a defect or changing matchmaking behavior across modules.
- Read `../rpc-unit-test/SKILL.md` for the offline mock runtime and `../testing/SKILL.md` for executable filters and
  Windows DLL lookup.
- Read `../build/SKILL.md` before configuring or building. Resolve `<BUILD_DIR>` from workspace settings and put all
  scratch output under `<BUILD_DIR>/_agent_tmp/...`.

## System map

- `resource/ExcelTables/Matching.xlsx`: designer-facing pool, rule, and result-template keys.
- `src/server_frame/protocol/public/protocol/config/com.struct.matching.config.proto`: generated configuration schema.
- `src/server_frame/config/{include,src}/excel_config_matching_index.*`: matching configuration indexes.
- `src/server_frame/protocol/public/protocol/pbdesc/com.{protocol,struct}.match.proto`: requests, events, state, scopes,
  teams, and units.
- `src/lobbysvr/service/logic/matching/`: user-facing start, check, confirm, cancel, and local ownership state.
- `src/lobbysvr/service/logic/action/task_action_matching_event_sync.cpp`: matchsvr-to-lobby event application.
- `src/matchsvr/service/logic/matching/`: buckets, rooms, rules, faction assignment, lifecycle, and WAL handling.
- `src/matchsvr/service/logic/action/`: inbound matching RPCs and Orbit room-ready callback.
- `src/matchsvr/test/`: room, rule, lifecycle, migration, and integration-style offline tests.

## Review or change workflow

1. Trace one complete request before editing:
   `lobbysvr start -> matching SDK/RPC -> manager bucket -> room -> confirm -> Orbit create -> room-ready -> event sync`.
   Record ownership, state, timeout, and error propagation at every boundary.
2. State the invariants being changed. At minimum check party atomicity, one-room/one-bucket ownership, scope isolation,
   faction capacity, deterministic removal, idempotent callbacks, and terminal cleanup.
3. Treat Excel keys and protobuf field names as one contract. Compare workbook key rows with protobuf fields, regenerate
   resources, and inspect the generated matching JSON. A successful generator exit does not prove unknown columns were
   consumed.
4. For lifecycle work, enumerate every state and exit: matching, confirming, creating battle, success, cancel,
   timeout, migration, failed callback, and service loss. Each nonterminal state needs a bounded recovery path.
5. For migration, draw subscriber changes and WAL publication in temporal order. Ensure the old owner can deliver the
   switch/remove event before it loses the subscriber, and that the new owner's add event cannot arrive first.
6. For scope or level changes, preserve the three-layer model:
   - `DMatchingScope` is only the coarse bucket: `level_type`, region, battle version, and pool.
   - Every Unit carries normalized `acceptable_level_ids`; all Units in a room must have a non-empty intersection.
   - The room independently owns `selected_level_id`, keeps it inside the current intersection, and Orbit uses it.
   `DMatchingScope` must never contain the selected or preferred level. Validate the client selection against server-owned
   level configuration before building the normalized candidate set. `DLevelSelect.level_ids` is a required, non-empty CS
   multi-select input; `level_id` is only the preferred candidate and, when nonzero, must belong to the list. Every candidate
   must resolve to the same requested `level_type` and server-owned matching pool.
7. For rule or faction changes, test boundary values, impossible assignments, multiple parties, and rollback after a
   failed placement. Treat a faction as a room-owned group with a capacity fixed when the faction is created; never
   resize or remap an existing faction to another template slot. Keep the result template dynamic while `MATCHING`:
   filter rule-provided templates by whether their capacity multiset contains the room's fixed faction capacities, and
   select the final template only when every required faction is present and full. Enforce these invariants:
   - Dynamic rule changes only widen eligibility. Time-window or population-driven rule selection must not invalidate
     any Unit combination or fixed faction-capacity multiset admitted earlier in the room lifetime, and rule windows must
     not leave gaps. After readiness evaluation, a non-empty `MATCHING` room therefore remains legal and joinable;
     failure to find a containing template indicates broken configuration or an internal invariant rather than a normal
     runtime branch.
   - Treat `min_total_user` and `start_battle_min_user` as room-level readiness thresholds. They gate only the final
     ready decision and must never prune result templates during room creation, Unit/faction placement, or matching-room
     legality checks; a strict rule may intentionally keep a room waiting until a later rule relaxes the threshold.
   - Keep every Unit atomic. A Unit that disables filling exclusively occupies a faction whose fixed capacity exactly
     equals the Unit size.
   - Put a Unit that enables filling into an existing compatible incomplete faction first. When a new fillable faction
     is required, choose the largest compatible faction capacity exposed by the active templates and lock it immediately.
   - Allow multiple incomplete factions after cancellation or other removals. Treat fill-before-create as placement
     priority, not as a room-validity constraint; never rebuild or rearrange preserved factions to remove extra gaps.
   - Delete an empty faction. Preserve the capacity and remaining membership of every non-empty faction.
   - Do not use whole-room backtracking for Unit placement, cancellation, readiness, or rebalance. Validate preserved
     assignments and candidate-template capacity counts incrementally.
   - Do not introduce a generic whole-room assignment-state evaluator. New rooms select their first faction from the
     capacity index; joins validate only incoming Unit compatibility plus the prospective capacity-count change; readiness
     checks only exact template equality and selects the final template. Existing room structure is an invariant checked
     at mutation boundaries or in tests, not a normal matchmaking branch.
   Prefer a room where the incoming Unit completes an existing faction, then one leaving the smallest gap, before a
   room that requires a new faction. For a new room's first Unit, use the capacity/count index directly.
   Rebalance is target-centric: process targets from oldest to newest and let an older target pull a bounded sequence of
   migration atoms from newer donors. Build and score one candidate frontier per target call; because adding members and
   consuming template capacity can only tighten feasibility, discard initially invalid candidates and lazily revalidate
   each retained atom against current source and target state immediately before committing it. Re-evaluate both rooms
   after every commit, but do not rescan every candidate after every atom. Bound both one target's migrations and total
   migrations per service tick. The old-to-new direction prevents
   a Unit from hopping through multiple rooms in one tick and keeps convergence deterministic. The migration atoms are
   one Unit from an incomplete faction, or every Unit of a complete faction
   together while preserving its capacity and membership. Never move only part of a complete faction, and never merge a
   complete donor faction into an incomplete target faction. A migration must strictly improve the target by filling an
   existing faction, adding a complete faction, or increasing the legal user count for a template without factions; it
   must not create a new incomplete faction. Require the preserved donor and prospective target capacity multisets to
   remain compatible with active templates. Re-evaluate readiness before rebalance and only migrate between rooms that
   are still searching; a ready donor or target must enter confirmation instead of being expanded or dismantled. Review
   algorithmic cost because matching executes from the service tick.
   During matching and confirmation, store only faction membership and capacity. Immediately before battle creation,
   assign deterministic battle `faction_id` values once, cache them for retries, and keep every Unit in one membership on
   the same ID. Never derive a battle ID from container iteration order or expose one before battle creation begins.
8. Keep room state and subscriber state separate:
   - `DMatchingRoomSnapshot` is matchsvr-internal diagnostic/test state and may contain every Unit.
   - SS RPC responses, WAL subscription sync, and lobbysvr persistence use the internal `DMatchingPlayerView`. It owns
     `matching_id`, the Matchsvr WAL cursor, the subscribed Unit, room lifecycle/result, selected level, Orbit handoff,
     and, after battle creation begins, that Unit's finalized `faction_id`.
   - CS responses and pushes use the smaller `DMatchingClientView`. It contains the stable `unit_id`, a Lobby-owned
     monotonic `view_revision`, lifecycle/result, expiry, selected level, and finalized `faction_id`; it never contains
     `matching_id`, Matchsvr WAL cursors, switch metadata, raw event logs, or temporary faction assignments.
   - Keep `unit_id` stable while the Unit migrates between matching rooms and allocate a new one for a new matching
     attempt. Cancel and confirm requests identify this stable Unit. Lobby must reject a stale Unit before forwarding a
     destructive request and then use its current internal `matching_id` for the SS RPC. Check requires no client room
     identifier or acknowledgement cursor.
   - Increment `view_revision` only for accepted room migrations or changes to fields projected into
     `DMatchingClientView`; a Matchsvr WAL cursor-only update must not advance it. Never reset it when the internal room
     changes. Clients discard a view whose `view_revision` is lower than the locally applied version. Lobby alone
     advances and persists the Matchsvr WAL cursor.
   - Group WAL delivery by `(lobbysvr server_id, unit_id)`, filter Unit add/remove events to that Unit, and never attach
     a full room snapshot to a client-visible matched event.
   - Keep old persisted full snapshots read-only and migrate them by locating the current player's Unit; new writes use
     only the player view.
9. Read [the current review baseline](references/review-baseline.md) when reviewing existing behavior, planning a fix,
   or touching any path named there. Reverify each item against the current checkout before reporting it as open.

## Test and validation requirements

- Assert `runtime.start(options) == 0`; do not log and return from a failed fixture setup because the private framework
  can still report the case as passed.
- Exercise real actions through `atframework::testing::invoke_ss_action<TAction>` when the test targets a known SS
  action. Set source metadata explicitly for forwarding and migration behavior.
- Cover the full state chain and negative paths: invalid configuration, duplicate request, confirmation timeout,
  battle-create failure, unsuccessful room-ready callback, migration ordering, and late/duplicate events.
- Discover the exact test target from `src/matchsvr/test/CMakeLists.txt`, build it with the configured build tree, and
  run both focused cases and the full executable. Inspect output for fixture-start failures and skipped prerequisites;
  exit code zero alone is insufficient.
- Regenerate matching resources after schema or workbook changes and assert required fields in generated output.
- For level-candidate changes, cover overlapping candidates, disjoint candidates, intersection widening after removal,
  rebalance of both target and remaining source rooms, final Orbit selection, and missing-candidate rejection.
- Finish with the checks routed by engineering guidelines and `git diff --check`. Do not rebuild for Skill-only edits.

## Review baseline maintenance

- Keep stable procedures in this file and dated, source-backed findings in `references/review-baseline.md`.
- Update or remove a baseline item when a fix lands; retain the invariant and regression-test expectation when it is
  still useful.
- Distinguish reproduced defects from scalability or availability risks. Never present a baseline observation as a
  current fact without checking the current code and generated output.
