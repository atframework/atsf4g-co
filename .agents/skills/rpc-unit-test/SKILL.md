---
name: rpc-unit-test
description: "Use when: designing, authoring, reviewing, or running offline RPC unit tests with the atfw::testing::runtime mock fixture in src/tools/rpc-unit-test (mock SS/DNS/DB/CS/router/transport/resource/hpa engines), registering mock rules, choosing feature flags, or adding a project_add_rpc_unit_test CMake target. For generic unit-test running/filtering/DLL-PATH issues use testing; for production RPC implementation use engineering-guidelines."
---

# RPC unit testing (offline mock fixture)

`src/tools/rpc-unit-test` provides an offline mock-RPC fixture: `atfw::testing::runtime` boots a real
`atapp::app` + dispatchers against `mock://` connectors and hook seams, so a test drives real coroutine tasks and real
generated RPC code without Redis/DNS/atbus/HTTP. Library API namespace is `atframework::testing`; test support code lives
only when `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` is on (default ON when `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST`,
forced OFF for production builds).

Full API/feature matrix is in `src/tools/rpc-unit-test/README.md`; read it when you need exact signatures. Read
`references/engine-invariants.md` only when **modifying** the mock engines/hook seams, not for authoring tests.

## Case-quality overlay

Read `../testing/references/test-design-and-acceptance.md` when designing, writing, or reviewing cases. Apply these
fixture-specific constraints in addition:

- Drive the real generated RPC/client path or the production action under test. Use the typed direct-action helper only
  when action behavior is the subject; use transport/dispatcher entry points when registration, envelopes, unknown RPCs,
  or malformed wire data are the subject. Do not bypass the layer named by the case.
- Build requests, responses, discovery nodes, and DB records from current generated types and a nearby verified fixture.
  Use the smallest contract-valid baseline plus the field under test. Extra IDs, labels, versions, readiness flags, and
  metadata need a source-backed prerequisite; never add them only because the current test otherwise fails.
- Enable only the runtime features crossed by the flow. Mock the external boundary with branch-specific results, then
  assert consumer-visible results, state, outbound payloads, and cleanup. Do not encode the current internal call order.
- A hard timeout only prevents a hung CTest run; business timeout behavior must be asserted through task/result/state.
  `delay_generations`, fixed pump counts, and the transport barrier are valid oracles only when engine scheduling is the
  subject of a fixture selftest, not when testing service business behavior.
- An unmatched one-way call is recorded and dropped with success, so success alone does not prove a required notification
  or broadcast occurred. Register an expectation or inspect captured calls/payloads and let `stop()` verify consumption.
- Assert call count/order only when retry, routing, or delivery ordering is the contract. If faithful mock setup is more
  complex than the real in-process component, move the scenario to the appropriate integration level.

## Canonical case pattern

```cpp
CASE_TEST(rpc_unit_test, my_case) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::db};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;  // CASE_EXPECT_* is non-fatal: you MUST early-return on precondition failure.
  }

  // Arrange: inject a mock:// discovery node and register rules (typed <service>::mock / <db>::mock or
  // engine-level test.ss()/test.db()/test.dns()/...). Then run the real coroutine logic:
  auto task = test.run_task("name", std::chrono::seconds{2}, [](rpc::context &ctx) -> rpc::result_code_type {
    int32_t res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, uid, store, version));
    RPC_RETURN_CODE(res);
  });
  if (task.empty()) {
    CASE_MSG_INFO() << task.get_diagnostic() << '\n';
    test.stop();
    return;
  }

  auto result = test.wait(task, std::chrono::seconds{5});  // hard timeout > task timeout
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(0, test.stop());  // stop() is idempotent; non-zero means failed expectation/unconsumed rule.
}
```

Key rules:

- **One active runtime per process** (app/dispatcher/task-manager singletons). CTest parallelism is across executables
  only; cases inside one executable run serially.
- `run_task` body uses `RPC_AWAIT_*`/`RPC_RETURN_CODE`; it always creates a real task via `rpc::async_invoke`.
- Consume every RPC return value by its type. APIs returning `rpc::always_ready_code_type`/`rpc::always_ready_void_type`
  (SS `broadcast::*`, CS downstream `send_*`/`broadcast_*`, all fire-and-forget generated calls) must be awaited
  or `.unwrap()`ed: the implicit `operator value_type()` does NOT mark the result
  awaited, and `PROJECT_SERVER_FRAME_*_COROUTINE_CHECK_AWAIT` builds assert in the destructor
  (`src/server_frame/rpc/rpc_common_types.h`). APIs returning `rpc::rpc_result<T>`/`rpc::result_code_type` must be
  awaited; never bind them to a plain value or discard them. Await only through the dual-coroutine adapter macros
  `RPC_AWAIT_CODE_RESULT`/`RPC_AWAIT_TYPE_RESULT`/`RPC_AWAIT_IGNORE_RESULT`/`RPC_AWAIT_IGNORE_VOID`; never write
  `co_await`/`co_yield` directly.
- Both timeouts exist: the **task timeout** (asserts business timeout) and `wait`'s **hard timeout** (guards CTest hang;
  on hit it kills all tasks and poisons the runtime, which then only allows `stop()`, never reuse).
- Put assertions **after** `wait()`; pass business failure through the task's return code.

## Driving an inbound SS action

For a service-internal test of a known `task_action_ss_rpc_base`, include
`<atframework/testing/ss_action.h>` and use the shared typed helper; do not duplicate SSMsg serialization or
`task_manager` create/start/wait plumbing:

```cpp
atfw::testing::ss_action_invoke_options invoke_options{
    rpc::my_service::packer::get_full_name_of_my_method()};
invoke_options.source.node_id = source_node_id;
invoke_options.source.node_name = "my-test-source";
invoke_options.source.source_task_id = source_task_id;
invoke_options.source.sequence = source_sequence;

int32_t result = RPC_AWAIT_CODE_RESULT(
    atfw::testing::invoke_ss_action<task_action_my_method>(ctx, request, invoke_options));
```

- Enable `feature::ss` and await the helper only from `runtime::run_task` or another real action coroutine.
- Use the generated `packer::get_full_name_of_<rpc>()`. The request type is fixed by `TAction::rpc_request_type`, and
  the helper validates both request and response descriptors against that method before creating the action.
- Request/options use by-value coroutine snapshots. Temporaries are safe; mutations after invocation do not affect the
  action.
- Zero source IDs mean anonymous/system. Set all relevant source fields explicitly for forwarding, tracing, replies,
  or source-sensitive authorization.
- The result is the action's final task result, not the response protobuf's business code.
- This is a typed direct-action path, not a dispatcher registration lookup. Use raw transport/dispatcher APIs when the
  subject is registration, unknown RPC names, or malformed type URL/body/envelope handling.

## feature flags (`runtime_options.features`)

`ss, dns, cs, db, uuid, resource, router, orbit, hpa, telemetry`. Each enables the matching dispatcher/hook; CMake
`FEATURES` only validates/links/labels and never decides per-case behavior — the case sets `features` itself. `uuid`
currently adds no runtime wiring (its two DB-backed entries flow through the db hook; the three local uuid functions are
pure). See README §"feature 标志" for the exact per-flag module/component set.

## Default behavior (unmatched calls)

- **One-way** (SS stream/no-wait/broadcast, CS downstream data/kickoff/set-router): unmatched = **record + drop,
  success**.
- **Response-required** (SS unary/wait variants, DNS lookup, HPA pull): unmatched = **immediate error + diagnostic**
  (never a silent wait-to-timeout).
- **DB** default = empty in-memory backend (reads return the project record-not-found code).

## Add a test target (CMake)

```cmake
project_add_rpc_unit_test(
  TARGET component_name_rpc_test
  COMPONENT component-name
  CATEGORY component          # component (default) | sdk | service
  SOURCES test_a.cpp
  LINK_LIBRARIES components::component_name_sdk
  FEATURES SS DNS DB
  LABELS fast
  TIMEOUT 120)
```

- Reuses the centralized private framework main/frame targets; registers `add_test`, labels
  (`<project>;unit;rpc-unit-test;${CATEGORY}:<name>;feature:<x>` + caller labels), CTest timeout, per-target build-tree
  working directory, and Windows `ENVIRONMENT_MODIFICATION` PATH. Never hardcode the project prefix in target names.
- `CATEGORY` controls the label prefix: `component` (default) for `component-functions.cmake` targets, `sdk` for
  library/SDK tests, `service` for service-internal tests. Never use `COMPONENT` as a catch-all — pick the category that
  matches what the test links (component SDK vs service internals vs framework library).
- `TIMEOUT` must exceed the worst **serial sum** of all cases' hard-timeout + teardown, not just one runtime timeout.

## Build & run

```bash
cmake --build build_jobs_cmake_tools --target atf4g-co-rpc-unit-test-selftest --parallel 12
ctest --test-dir build_jobs_cmake_tools -L rpc-unit-test --output-on-failure
# Filter a case (direct run): build_jobs_cmake_tools/publish/bin/atf4g-co-rpc-unit-test-selftest.exe -r "rpc_unit_test.<case>"
```

## Gotchas (these bite; do not re-derive)

- `delay_generations`: `0` = completes in the pump that observed the request; `N` = `N` extra full pump generations.
  Consistent across SS and DNS engines.
- Delivery is **due-order, not FIFO**: with mixed delays, a queued-early slow event must not block a queued-later fast
  one. Engines drain ALL due events per pump.
- Delivery runs real dispatch **synchronously**; a resumed coroutine may immediately re-enter the engine
  (`inject_response`/`queue_response` `push_back` invalidates deque iterators). Never hold deque iterators across
  `trigger_event_on_forward_request`/`custom_resume`; use the two-phase `detail::drain_due_events`.
- mock_db mirrors the Redis/Lua golden contract (the real scripts are executed in an embedded Lua
  interpreter against a `redis.call` simulator by `src/server_frame/test/server_frame_test_db_script_contract.cpp`;
  the same contract is pinned at the engine level by `src/tools/rpc-unit-test/test/rpc_unit_test_db.cpp`; when a script
  changes, keep the mock and the mirroring cases in sync): `EXPIRE`/`PERSIST` on a **missing key is a successful
  no-op** (NOT an error); a
  CAS set accepts a record without a version, an equal expected version, **or expected version 0 (ignore the CAS check
  and force the overwrite — the stored version still bumps from the real one)**; otherwise the conflict returns
  `EN_DB_OLD_VERSION` and writes the stored version back; insert (`op_type::kv_insert`) rejects an already-versioned
  record with `EN_DB_KEY_EXISTS`; every write path (HSET/CAS/insert) merges only the fields present in the store
  message (never replaces the whole hash); KL add allocates 1-based monotonic never-reused indexes and evicts exactly
  the smallest index at `max_list_length` (0 keeps one entry); `inc_field` creates from 0 and never touches the CAS
  version.
- Transport barrier: an outbound record captured at generation `N` is consumable at `N+2` (one full pump between
  capture and consume), so the calling task has already registered its waiter before the mock responds.
- Generated mock (`<service>::mock`, `<db>::mock`) is compiled into server_frame/service TUs and must **not link** the
  tool library; it registers through the type-erased bridge `rpc/unit_test/mock_engine_bridge.h` (empty bridge →
  no-op/empty handle). Engine-level `test.ss().mock`/`test.db().mock_table` remain for direct test use.
- For engine-level `test.ss().mock/expect/calls` always pass the RPC full name via the generated
  `rpc::<module>::packer::get_full_name_of_<rpc>()` (`gsl::string_view`, declared in `<service>.atfw.gen.h`; the component SDK
  headers do not include it, so include the gen header in the test TU) — never hardcode `"pkg.Service/method"`
  strings; keep hardcoded names only for intentionally invalid negative tests. This applies to SS/CS-template services
  (slash wire name `package.Service/method`); the orbit-fork getter returns its dot-format protocol name and orbit RPCs
  traverse orbit transport (not the SS engine), so do **not** pass it to `test.ss()`.
- Mock handlers return `rpc::result_code_type` and may be coroutines awaiting nested RPC (SS handler driven by engine
  `async_invoke`; DB handler `co_await`ed at the generated interface entry).
- `working_directory` empty → env `RPC_UNIT_TEST_WORKDIR` (ctest per-target) → baked `RPC_UNIT_TEST_DEFAULT_WORKDIR`
  (build tree) → CWD. Never write the generated `rpc-unit-test.yaml` into the repository root.
- Conflicting telemetry-config fixtures must be split into **separate executables** (`project_add_rpc_unit_test` multi
  target): exporter/provider state is process-lifetime; the first telemetry-configured fixture in an executable wins.

## Validation

- After changing engine behavior: rebuild selftest and keep `ctest -L rpc-unit-test` green; add/extend cases in
  `src/tools/rpc-unit-test/test/` to pin the new semantics (delay, out-of-order, TTL/CAS, barrier).
- Windows ninja header-dependency tracking is broken in this tree (`#deps 0`): after editing a header, delete the
  affected `.obj` under `<BUILD_DIR>/.../<target>.dir` before rebuilding (never `ninja -t clean`). See the build skill.

## References

- Full API, feature matrix, offline matrix, examples: `src/tools/rpc-unit-test/README.md`
- Design & usage doc (published): `doc/docs/development/rpc-unit-test.md`
- Engine internals (only when modifying the engines/hook seams): `references/engine-invariants.md`
- Generic private test framework (run/filter/DLL-PATH): `../testing/SKILL.md`
- Style/ABI/lint: `../engineering-guidelines/SKILL.md`
