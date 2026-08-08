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

## RPC Unit Testing (Offline Mock)

Implemented: `src/tools/rpc-unit-test/` runs real generated RPC/dispatcher/task code **without real
Redis/DNS/atbus/gateway**, replacing all external dependencies (SS/DNS/CS/DB/UUID/resource/HPA/telemetry) with
in-memory mock engines. See [RPC Unit Testing (Offline Mock)](rpc-unit-test) for the full working principle, usage
guide, and semantics contract.
