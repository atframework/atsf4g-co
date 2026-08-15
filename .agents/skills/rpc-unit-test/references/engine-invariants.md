# rpc-unit-test engine invariants

Load this only when **modifying** the mock engines (`mock_ss`, `mock_dns`, `mock_db`, `mock_cs`, `raw_transport`,
`mock_connector`, `runtime`, `src/detail/pending_drain.h`) or the server-frame hook seams. For authoring tests, the
`SKILL.md` gotchas plus `src/tools/rpc-unit-test/README.md` are sufficient. Distilled from the implementation; the
published design/usage overview is `doc/docs/development/rpc-unit-test.md`.

## Pump / generation model

`runtime::pump_once()`:

1. `atfw::util::time::time_utility::update()` (cached clock; task timeouts and DB TTL depend on it);
2. first `app.run_noblock()` — tasks run, outbound calls are captured;
3. `++pump_generation` (transport generation); the dns/ss engines advance their own counters once per `deliver_pending`;
4. `transport/ss/dns.deliver_pending()` — deliver what is due;
5. second `app.run_noblock()` — resumes the waiting tasks.

`runtime::wait()` loops `pump_once()` until the task exits or the wall-clock hard deadline hits; it checks task
success/cancel/fault/timeout via `task_type_trait` and marks the runtime poisoned on hard timeout (`task_manager` only
exposes `kill_all`, so a hard timeout cannot claim to reclaim just one task).

## Delivery ordering & re-entrancy (the two-phase contract)

- **Due order, not insertion order.** Rules carry different `delay_generations`, so insertion order != due order. Never
  stop a delivery scan at the first not-due entry; drain ALL due events.
- **Re-entrancy.** Delivering runs the real dispatch synchronously
  (`app::trigger_event_on_forward_request` / `rpc::custom_resume`); the resumed coroutine may immediately re-enter the
  engine (`inject_response` / `queue_response` → `deque::push_back` invalidates iterators). Therefore:
  `detail::drain_due_events(queue, current_generation)` (in `src/detail/pending_drain.h`) first **moves the due batch
  out**, then the engine delivers from the local batch. This is the single canonical implementation — both
  `mock_dns::deliver_pending` and `mock_ss::deliver_pending` call it; do not re-implement per engine.
- `delay_generations`: `deliver_at = queue_gen + 1 + delay`. The `+1` matches the "increment counter first, then deliver
  everything `<= current`" model, so `delay == 0` completes in the pump that observed the request and every extra
  generation is one more full pump. SS and DNS must stay consistent (the DNS comment cross-references the SS engine).

## Transport barrier

- An outbound record captured at generation `N` is consumable at `N+2`
  (`raw_transport::collect_outbound` breaks when `record.pump_generation + 1 >= current`). This guarantees the current
  pump's coroutines finish (the waiter is registered) before the record triggers mock response injection.
- `inject_inbound` and the send-ack use `deliver_at = current + 1`, delivered when `deliver_at < current` (i.e. `N+2`).
- `capture_outbound` does only no-user-code preflight; typed request materialization and the user handler run in a later
  pump generation, and the handler output is re-serialized into owned bytes before re-injection (no borrowed spans /
  Arena / caller-task objects are held).

## mock_db golden contract (mirrors Redis commands + embedded Lua)

The authoritative write contract is the Lua script set embedded in
`src/server_frame/dispatcher/db_msg_dispatcher.cpp` (`kCompareAndSetHashTable`, `kInsertHashTable`,
`kAddListIndexHashTable`). Their **observable behavior** (not the script text) is pinned by
`src/server_frame/test/server_frame_test_db_script_contract.cpp` (server_frame level, through the `rpc::db::hash_table`
primitives) and `src/tools/rpc-unit-test/test/rpc_unit_test_db.cpp` (engine selftest level): read/write version
consistency, version-0 forced overwrite, insert-only rejection, field merge and KL eviction. When a script changes,
re-verify the new semantics against those cases and update the mock together. The in-memory backend must also
reproduce the dispatcher-layer error mapping itself because the hash-table hook bypasses `db_msg_dispatcher::dispatch`
(the hook seam sits inside the `rpc::db::hash_table` entry, so caller-side reply mappings such as insert's
`CAS_FAILED` → `EN_DB_KEY_EXISTS` conversion are part of the mock's contract):

- `EXPIRE`/`PERSIST` (`set_ttl`/`remove_ttl`) on a **missing key is a successful no-op** — production maps the plain
  integer reply (`0`) through `unpack_nothing` → `EN_SUCCESS`. Do NOT return not-found for these two ops.
- CAS (`kv_set` with a version pointer → `kCompareAndSetHashTable`): accepted when the stored version is 0 (missing or
  never-versioned record), when the expected version equals the stored one, **or when the expected version is 0
  (ignore the CAS check and force the overwrite)**. On success the stored version becomes `real_version + 1` (never
  `expected + 1`) and is written back; a mismatched non-zero expected version returns `CAS_FAILED|<real_version>`
  mapped to `EN_DB_OLD_VERSION` **and writes the current version back** to the caller without touching the record.
- Insert (`kv_insert` → `kInsertHashTable`): accepted only when the stored version is 0 — including a record that only
  carries unversioned HSET data (no `CAS_VERSION` field in the hash). An already-versioned record returns
  `EN_DB_KEY_EXISTS` (caller-mapped `CAS_FAILED`) with the stored version written back; on success the version starts
  at 1.
- KV writes (`set` with or without CAS, `insert`) merge only the caller message's **present fields** — `HSET`
  semantics (`rpc::db::pack_message` HSETs `ListFields` output), never replacing the whole hash; the mock implements
  this with `mock_db::merge_stored_data`. A versionless `set` never touches the stored CAS version. `partly_get`/
  `HMGET` depend on field presence; the backend must be presence-aware.
- `inc_field` follows `HINCRBY`: missing key/field starts from 0; field descriptor/numeric validation happens before the
  hook; the CAS version is never touched and the caller message is swapped to hold only the incremented field.
- KL index comes from `HINCRBY` on the counter field: 1-based, monotonically increasing and **never reused**. The
  counter field name in `kAddListIndexHashTable` must stay equal to `REDIS_LIST_INDEX_FIELD` (`"__index_number"`) —
  the unpack path skips that field, and any other name leaks the counter as a data entry with a non-`'&'` value and
  fails every KL read. The script counts every hash field (counter included), so an add at capacity
  (`entries >= max_list_length`, also for `max_list_length == 0`) evicts exactly one entry — the **smallest index**,
  which after `update_by_index`/`remove_by_index` is not necessarily the oldest insertion position — before appending
  the next monotonic index. `get_by_indexs`/`HMGET` keep request order and a one-to-one result slot (missing entries
  are not collapsed); `update_by_index` may upsert a field but must not touch the counter.
- Writing (add_index/update_by_index) to a lazily-expired key starts a **fresh record without the stale TTL** — purge
  expired records via `find_live_*` before indexing the record map (matches the KV paths).

## Hook seam & ABI rules

- `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` (`cmake_dependent_option` in `project/cmake/ProjectBuildOption.cmake`):
  ON when `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST`, else forced OFF. Generated via
  `server_frame_build_feature.h.in` `#cmakedefine01` (not `NDEBUG`) so Release unit tests and multi-config builds are
  consistent. `#if`-guards wrap the registry, setters and call sites; an OFF build carries **zero** symbols, state, or
  hot-path branches, and production fallback is preserved until a fixture installs a hook.
- Each seam's registry is defined in **one `.cpp` in the owning production DLL** (core SS/DNS/CS/DB in `server_frame`,
  Excel provider in `server_frame-config`, Orbit client in its SDK). Never use header-local statics, inline globals, or
  cross-DLL registry copies; the tool library never owns a production global entry. Registry holds shared state +
  generation token; queues own bytes/messages/metadata only — no dangling `gsl::span`/`string_view`/Arena/fixture
  pointers.
- Generated mock (`<service>::mock`, `<db>::mock`) is compiled into server_frame/service TUs and must **not link** the
  tool library. It registers via the type-erased slots in `rpc/unit_test/mock_engine_bridge.h`: `register_ss_rule`
  (returns a `shared_ptr<void>` token whose deleter deactivates the rule) and `db_register_typed_handler` (hands a
  clear-closure to the engine's lifecycle). The engine installs slots at `bind()` and clears them at `unbind()`; an
  empty bridge degrades the generated helper to empty-handle/false/no-op. Engine direct APIs (`mock_ss::mock`,
  `mock_db::mock_table`) stay for tests to use directly.
- Bridge mirror types live in server_frame: `ss_mock_rule_options`, `ss_mock_request_view`, `mock_rule_handle`,
  `db_mock_meta`.

## Offline matrix (fixture must keep these off or hooked)

- **etcd**: not configured (service discovery uses injected global-discovery nodes; enabling etcd blocks `uv_run` on
  real HTTP).
- **atbus listen**: empty by default; a hostname triggers `uv_getaddrinfo` — use numeric loopback IP or `shm`/`unix`.
- **atbus upstream/proxy**: forbidden (init blocks connecting atproxy).
- **Redis**: the DB dispatcher is never added/`init()`ed in a fixture (hooks-on `set_record_info_for_unit_test` sets the
  deterministic `unit-test`/`RAW_DEFAULT` fields without `init()`).
- **telemetry**: no exporter by default (Noop provider); only file/`ostream` exporters are allowed in tests — never
  `otlp_grpc`/`otlp_http`/`prometheus_push` (outbound) or `prometheus_pull` (inbound listen); the runtime fail-fasts on
  them. Trace provider additionally needs `trace.exporters` **and** `trace.processors` (simple or batch).
- **HPA**: off by default; enabling the feature installs the default prometheus pull hook.
- **DNS**: hooked before `uv_getaddrinfo`.

## Process-lifetime / isolation gotchas

- Only **one active runtime per process** (app/dispatcher/task-manager/config singletons); CTest parallelism is across
  executables only. Serial consecutive fixtures must not leak app/module/task/provider state (each engine's `unbind()`
  fully resets its state).
- telemetry exporter/provider and the `excel_config_wrapper` production loader/group wrapper are **process-lifetime**
  with no remove API: teardown only clears the scoped provider, and conflicting telemetry fixtures must be split into
  separate executables.
- `start()` rolls back in reverse stage order on mid-failure; `stop()` verifies expectations/unconsumed rules before
  unbinding, drains to `app.is_closed()` under a teardown deadline, and clears every scoped hook/provider.
