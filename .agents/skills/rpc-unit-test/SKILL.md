---
name: rpc-unit-test
description: "Use when: writing, reviewing, or running src unit tests that exercise project RPC paths, use atfw::testing::runtime or mock engines, require async test hooks, or use project_add_rpc_unit_test. For ordinary unit tests and generic execution/filtering/PATH use testing; for production RPC code use engineering-guidelines."
---

# RPC and async-hook unit tests

`src/tools/rpc-unit-test` runs real application, dispatcher, coroutine task, and generated RPC paths against offline
mock boundaries. Its target helper also owns `src/**` unit tests that require the project's async test hooks, even when
the main case is not an RPC contract. Use `src/tools/rpc-unit-test/README.md` for current APIs, feature names, CMake
arguments, and examples; do not copy that catalog into this Skill.

Read `../testing/references/test-design-and-acceptance.md` when designing or reviewing cases. Read
`references/engine-invariants.md` only when changing mock engines or diagnosing scheduling, delivery, process-lifetime,
or offline-boundary behavior.

## Workflow

1. Verify the behavior from the production action, generated contract, configuration, and a nearby current test.
2. Choose the entry point that owns the contract. Drive a real generated client/action path for normal behavior; use
   transport or dispatcher entry points for registration, envelope, unknown-RPC, or malformed-wire behavior.
3. Enable only the runtime features crossed by that path. Build the smallest contract-valid request, discovery state,
   and stored records before adding the field or failure under test.
4. Use the lifecycle below: start one runtime, register boundary rules, run the production coroutine, wait, assert, and
   stop.
5. Discover the target and labels from the nearest `CMakeLists.txt`. Resolve `<BUILD_DIR>` through the build Skill,
   then run the focused case and the full affected executable or label group.

## Minimal runtime lifecycle

```cpp
CASE_TEST(rpc_unit_test, framework_flow) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;  // CASE_EXPECT_* is non-fatal.
  }

  // Register mock boundary rules, then call the production path from run_task.
  auto task = test.run_task("framework_flow", std::chrono::seconds{2},
                            [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_EQ(0, test.stop());
}
```

## Case rules

- Do not bypass the layer named by the case or reproduce production serialization and task plumbing in the test.
- Mock only the external boundary. Assert returned data, persisted state, outbound payloads, and cleanup. Assert internal
  call count or order only when retry, routing, or delivery ordering is the contract.
- Run coroutine bodies through the project `RPC_AWAIT_*` and `RPC_RETURN_CODE` adapters. Await or unwrap every RPC
  result according to its type; never discard a fire-and-forget wrapper or write direct `co_await`/`co_yield` in project
  code.
- A successful unmatched one-way call is only recorded and dropped. Register an expectation or inspect captured calls
  when the notification or broadcast is required.
- Keep business timeout assertions separate from the wait hard timeout. A hard timeout guards CTest from hanging; it
  kills all tasks and leaves the runtime usable only for `stop()`.
- A non-zero `stop()` reports failed expectations, unconsumed rules, or stale captured responses; treat it as a test
  failure.
- Use generated RPC-name accessors for valid calls. Hardcode a wire name only when an intentionally invalid name is the
  input under test.
- If faithful mock setup is more complex than the real in-process dependency, move the scenario to the appropriate
  integration level.

## Process and fixture isolation

- Only one runtime may be active in a process. Cases in one executable run serially, but process-global state can
  survive a runtime restart; reset it or separate incompatible cases into different executables.
- Telemetry provider configuration is process-lifetime. Put conflicting configurations in separate executables.
- The DTMQ subscriber manager and mock WAL are process-global, and a channel watermark never decreases. Use distinct
  channel identifiers for cases that inject or replay events, including cases separated only by a runtime restart.
- A DTMQ channel snapshot clears that channel's WAL. Start the next incremental-log hash chain from zero.
- DTMQ typed callbacks dispatch by `Any` message type. A mismatched `type_url` is filtered before the callback and does
  not advance the watermark; a matching type with a corrupt payload reaches the callback and advances the watermark
  before the payload is rejected. Assert the boundary being exercised.
- Register required dispatcher handles once per runtime. Explicitly consume responses captured by a downstream mock so
  stale-response cleanup does not fail teardown.
- Treat global clock offsets and monotonic timer wheels as process-wide state. Restore offsets with RAII and never make
  a later case observe time below an earlier case's high-water mark.

## Target and timeout rules

- Under `src/**`, add a unit-test target through `project_add_rpc_unit_test` when any case exercises project RPC paths or
  requires async test hooks guarded by `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`. This rule applies to a target that
  mixes ordinary and RPC/hook-dependent cases. Use `project_add_normal_unit_test` for an ordinary target with neither
  dependency.
- Do not reproduce target creation, private-framework sources, links, dependencies, PCH, post-build events, RPATH,
  runtime environment, or CTest labels in the consuming `CMakeLists.txt`. Use the category that matches the linked layer
  and do not hardcode the project prefix. Consult the fixture README for the current argument set.
- Set the CTest `TIMEOUT` above the serial sum of all case hard timeouts and teardown in that executable, not above one
  case timeout only.
- Keep generated runtime configuration in the target's build-tree working directory. Never write
  `rpc-unit-test.yaml` or other scratch output into the repository root.

## Validation

- For test-only changes, build the exact target, run the focused cases, then run the full affected executable or label
  group. Inspect output for fixture-start failures and skipped prerequisites; exit code zero alone is insufficient.
- For mock-engine or hook changes, rebuild the self-test and run the complete `rpc-unit-test` label. Add engine-level
  cases for the changed scheduling, ordering, boundary, or storage rule.
- Apply the engineering-guidelines checks and `git diff --check`. Use the build Skill's Windows header-dependency
  procedure when a changed header is not rebuilt.

## References

- API, feature matrix, CMake options, and examples: `src/tools/rpc-unit-test/README.md`
- Published design and usage: `doc/docs/development/rpc-unit-test.md`
- Engine internals and failure diagnosis: `references/engine-invariants.md`
- Generic unit-test execution, filtering, and PATH: `../testing/SKILL.md`
- C++/RPC style, ABI, and lint: `../engineering-guidelines/SKILL.md`
