---
title: Unit Testing
---

# Unit Testing

## Existing Tests

- The framework uses atframe_utils' private test framework (`CASE_TEST`, not GTest);
- Component tests live in `src/component/test/` (e.g., `ItemAlgorithmTest` for inventory slot algorithms);
- Build: configure with `-DPROJECT_ENABLE_UNITTEST=YES`, then run with CTest:

```powershell
cmake --build <BUILD_DIR> --config Debug
ctest --test-dir <BUILD_DIR> -C Debug --output-on-failure
```

If tests fail to start on Windows (DLLs not found), check that PATH includes the dependency DLL directories
(see `.agents/skills/testing/`).

## RPC Unit Testing (Planned)

`src/tools/rpc-unit-test/IMPLEMENTATION_PLAN.md` designs an RPC-level unit test support library, aiming to
run real generated RPC/dispatcher/task code **without real Redis/DNS/atbus/gateway**:

- `mock://` atapp connector + injected global discovery nodes to simulate SS/router RPC;
- DNS hook: intercept before `send_dns_lookup` initiates `uv_getaddrinfo`;
- DB seam: hooks added to each `hash_table` operation "after task validation, before constructing Redis
  commands", with a default in-memory backend (reproducing CAS version write-back / KL monotonic index
  semantics);
- Unified provider hook for the five UUID entry points; scoped resource provider for Excel configuration;
- Event pump: `app.run_noblock()` + `time_utility::update()`;
- New switch `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` (zero overhead when OFF), requiring dual-build
  verification with std-coroutine ON/OFF;
- CMake function `project_add_rpc_unit_test` automatically registers with CTest.

The first verification target: `hello.RouterService/router_transfer`. Usage examples will be added to this
section once the implementation lands.
