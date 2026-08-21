# Matchmaking review baseline

This baseline records findings reproduced during the 2026-08-14 review of the working tree. It is a starting checklist,
not a substitute for checking current code. Mark an item resolved only after verifying the implementation, generated
resources, and a regression test.

## Reproduced defects

### P1: Excel keys and protobuf fields diverge silently

Evidence at review time:

- `com.struct.matching.config.proto` defines `unit_max_size` and `faction_template`.
- The `Matching.xlsx` key rows use `team_max_size` and `team_template[...]` instead.
- Other workbook keys include `global_uesr_upper`, `min_total_player`, `user_force_limit`, and
  `team_templatem[2].count`; they do not match the corresponding protobuf contract.
- A fresh xresloader generation completed successfully but silently omitted the unmatched fields. Generated pools lost
  the party-size limit, result-template records contained only IDs, and matching rules lost minimum-player/force
  constraints.

Impact: invalid generated configuration can disable limits or leave faction capacities empty, causing pools that refer
to those templates to reject or mis-handle matching requests.

Required fix evidence:

- Reconcile every workbook key with the protobuf field path; do not add compatibility aliases without a migration need.
- Regenerate and assert representative values in the published matching JSON.
- Add a validation test that fails when required rule/template fields disappear.

### P1: Migration can remove the old subscriber before publishing the switch event

Evidence at review time:

- `matching_manager.cpp` subscribes the target, unsubscribes the source, then publishes the source removal and target
  addition.
- `matching_wal_handle.cpp` configures removed subscribers not to receive a final broadcast.
- The lobby depends on the source removal/switch event to establish the pending target; a target add arriving first is
  treated as unexpected.

Impact: a moved user can miss `switch_to_matching_id`; the lobby may discard later target events and remain attached to
the wrong matching owner.

Required fix evidence:

- Pin the intended WAL delivery ordering in an offline test with explicit source metadata and both owners.
- Keep the old subscriber able to receive the switch/remove record before unsubscribe becomes effective.
- Prove duplicate and reordered records remain idempotent.

### Accepted 2026-08-19: Lobby owns matching confirmation

The current product flow intentionally keeps confirmation in lobbysvr: after receiving `notify_confirm`, lobbysvr sends
the positive confirmation without waiting for an additional CS decision. Do not report this as a defect unless the
product contract changes back to client-owned accept/decline confirmation.

### Resolved 2026-08-19: Creating-battle state had no bounded recovery

Entering `CREATING_BATTLE` now sets an independent deadline using the matching pool's existing
`search_timeout_seconds`. Tick expiry marks the room `BATTLE_START_FAILED`, publishes the terminal event, and releases
all Unit/player indexes. Late ready callbacks return the retained failure without changing state. Regression coverage
checks callback loss, cleanup, and a late success callback after timeout.

### Resolved 2026-08-18: Scope and selected level had conflicting ownership

The server now uses a coarse `DMatchingScope` (`level_type`, region, battle version, pool), normalized per-Unit
`acceptable_level_ids`, and a room-owned `selected_level_id`. Room placement and both sides of rebalance require a
non-empty candidate intersection; Orbit reads the selected value. `DMatchingScope.level_id` was removed and protobuf
tag/name 2 are reserved so level ownership cannot drift back into the coarse bucket. CS exposes required, non-empty
`DLevelSelect.level_ids`; Lobby normalizes the set and validates that every candidate has the requested type and one
server-owned matching pool. Regression tests cover overlap, disjoint candidates, removal, rebalance, and missing-candidate rejection.

### Resolved 2026-08-18: Matchsvr tests could pass after fixture startup failed

`start_runtime` now asserts both `runtime.start(options) == 0` and `runtime.is_running()` before retaining the defensive
early return. The full executable was run with all 31 test cases entering their bodies and passing.

### Resolved 2026-08-18: Full room snapshots crossed the subscriber and CS boundary

`DMatchingRoomSnapshot` is now retained as matchsvr-internal room state. Direct SS replies, WAL sync, lobbysvr
persistence, and CS replies use a Unit-scoped `DMatchingPlayerView`. WAL subscribers are grouped by lobbysvr and
`unit_id`; add/remove events for other Units are filtered, and matched events no longer carry the full snapshot.
Legacy persisted snapshots are read once to locate the current player's Unit and are rewritten as player views.
Regression coverage checks legacy migration, identical lobbysvr/CS views, own-Unit isolation, and temporary faction
non-disclosure.

### Resolved 2026-08-19: Orbit room-ready failure bypassed ownership and state checks

Both successful and unsuccessful callbacks now require the Orbit selected by the room. A current failure transitions
only `CREATING_BATTLE` to `FAILED`; wrong-source callbacks are rejected, while duplicate or late callbacks return the
existing terminal result without regressing state. Regression coverage checks wrong source, first failure, duplicate
failure, and success after failure.

## Risks requiring design confirmation

### Tick-time faction placement work

The agreed invariant is incremental placement over room-owned, fixed-capacity factions. Do not use whole-room
backtracking to normalize cancellation, template changes, joins, readiness, or rebalance. Select templates dynamically
by capacity-count containment while searching and exact capacity-count equality when ready. Rebalance is target-centric:
process older targets before newer targets and pull only from newer donors. Within per-target and per-tick budgets, move
multiple atoms sequentially. Build one candidate frontier, then lazily revalidate each retained atom immediately before
commit and re-evaluate both rooms afterward; do not rescan the full frontier after every move. Each atom is either one
Unit from an incomplete faction or one complete faction. The move must fill an existing target faction, add a complete faction, or
increase users for a no-faction template; it must not create a new incomplete target faction. Matching and confirmation
retain only membership and capacity. Generate deterministic battle faction IDs once, cache them, and expose them only
when battle creation begins. Benchmark representative and adversarial room/faction counts and add deterministic limits
if the service-tick budget is exceeded.

### Availability and ownership

Matchsvr selection is first-ready rather than sticky, while active room state is primarily in memory. Confirm the
service-loss contract: retry destination, WAL replay ownership, duplicate suppression, and what the lobby tells clients
during recovery. Add a failover test before treating service discovery as sufficient recovery.

### Incomplete rule inputs

Lobby unit construction fills role level and search time but not rank level, while the implemented spread rule is based
on rank difference. Confirm the intended source for every rule input and reject or default missing fields explicitly;
do not let zero-valued protobuf defaults silently satisfy a rule.

## Regression matrix

At minimum preserve cases for:

- Solo and multi-member parties remain atomic through room placement and target-centric migration.
- Exact non-fillable faction capacity, fixed maximum-capacity fillable placement, multiple incomplete factions after
  removal, fill-before-create, dynamic final-template selection without capacity remapping, preferred pending-room
  selection, incomplete-faction Unit rebalance, atomic complete-faction rebalance, bounded multi-atom target fill with
  state recomputation, no new incomplete faction during rebalance, source/target readiness before rebalance,
  deterministic late faction-ID finalization, impossible assignment, and rollback.
- Same coarse-bucket requests share a room only while all Unit level-candidate sets have a non-empty intersection;
  disjoint candidates stay separate before and during rebalance.
- Confirm accept, decline, duplicate, disconnect, timeout, and late response.
- Orbit create error, `start_success=false`, callback loss, duplicate callback, and late callback after cleanup.
- WAL switch/remove/add delivery under reordered and duplicate records.
- Runtime fixture startup failure is visible as a failed test, not a skipped-success case.
- Generated resources retain every required pool, rule, constraint, and faction-template field.
