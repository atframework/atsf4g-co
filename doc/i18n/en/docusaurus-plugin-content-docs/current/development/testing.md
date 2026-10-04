---
title: Unit Testing
---

# Unit Testing

## Quick Start

- The framework uses atframe_utils' private test framework (`CASE_TEST`, not GTest);
- Component/service tests live in their module's `test/`; shared algorithms also use `src/component/test/`;
- Build: configure with `-DPROJECT_ENABLE_UNITTEST=YES`, then run with CTest:

```powershell
cmake --build <BUILD_DIR> --config Debug
ctest --test-dir <BUILD_DIR> -C Debug --output-on-failure
```

If tests fail to start on Windows (DLLs not found), check that PATH includes the dependency DLL directories
(see `.agents/skills/testing/`).

## RPC Unit Testing (Offline Mock)

Implemented: `src/tools/rpc-unit-test/` runs real generated RPC/dispatcher/task code **without real
Redis/DNS/atbus/gateway**, replacing supported framework dependency paths (SS/DNS/CS/DB/UUID/resource/HPA/telemetry) with
in-memory mock engines. See [RPC Unit Testing (Offline Mock)](rpc-unit-test) for the full working principle, usage
guide, and semantics contract.
