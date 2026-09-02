---
name: testing
description: "Use when: designing, writing, reviewing, or running generic unit tests, adding ordinary targets with project_add_normal_unit_test, filtering private-framework cases, or fixing Windows test startup/PATH. For src tests that exercise project RPC paths or require async test hooks, use rpc-unit-test."
---

# Unit testing (atsf4g-co)

This repository uses a private unit testing framework shared by several atframework projects.

Read [test design and acceptance](references/test-design-and-acceptance.md) when planning, writing, or reviewing cases.
It is not needed merely to run a known test command.

## Target registration

- Add every ordinary C++ unit-test target in this root project through `project_add_normal_unit_test`.
- For a target under `src/**`, use `project_add_rpc_unit_test` and the `rpc-unit-test` Skill instead when any case
  exercises project RPC paths or requires async test hooks guarded by `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`.
  This includes executables that mix ordinary cases with RPC/hook-dependent cases.
- Consuming test `CMakeLists.txt` files must not call `add_executable`, `atframe_add_test_executable`, or `add_test`
  directly. Pass the sources and settings supported by the selected helper instead of reproducing target setup.

## Discover and run tests (generic)

Most test executables support:

- List test cases: `-l` / `--list-tests`
- Run by group or case: `-r <group>` or `-r <group>.<case>`
- Filter (wildcards): `-f "pattern*"` / `--filter "pattern*"`
- Help/version: `-h`, `-v`

## Windows: DLL lookup via PATH

On Windows, unit tests/samples can fail to start if dependent DLLs are not found. Tests registered with CTest already
receive PATH through `ENVIRONMENT_MODIFICATION`; set PATH manually only when running an executable directly.

Test executables land in `PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY` (default `<BUILD_DIR>\test`) and samples in
`PROJECT_SAMPLE_RUNTIME_OUTPUT_DIRECTORY` (default `<BUILD_DIR>\sample`); the per-config output directories are pinned
to these paths, so no `<Config>` subdirectory is appended on multi-config generators. Typical DLL directories:

- `<BUILD_DIR>\publish\bin` (project DLLs)
- `<REPO_ROOT>\third_party\install\windows-amd64-msvc-19\bin` (third-party DLLs when using the bundled cmake-toolset)

Example (PowerShell):

```powershell
$buildDir = "<BUILD_DIR>"
$env:PATH = "$buildDir\test;$buildDir\publish\bin;${PWD}\third_party\install\windows-amd64-msvc-19\bin;" + $env:PATH
& "$buildDir\test\atframe_utils_unit_test.exe" -l
```

## Etcd-dependent tests

Read [local etcd tests](references/local-etcd.md) only when running `atapp_etcd_cluster`, `atapp_etcd_module`, or when
test output says an etcd-dependent case was skipped.

## Tip: using VS Code tasks

If the workspace provides CMake build tasks, prefer those to ensure UTF-8 console output and consistent build directories.

Read the executable's exit status and case counts. A skipped dependency-backed case is not passing coverage; report the
skip and the missing prerequisite explicitly.

When test code calls RPC interfaces (any `atfw::testing::runtime`-based fixture): consume the return value by its
declared type — `rpc::always_ready_*` (broadcast/fire-and-forget sends) must be awaited or `.unwrap()`ed, and
`rpc::rpc_result<T>` must be awaited; unconsumed values assert in CHECK_AWAIT builds. Await only via the adapter macros
`RPC_AWAIT_CODE_RESULT`/`RPC_AWAIT_TYPE_RESULT`/`RPC_AWAIT_IGNORE_RESULT`/`RPC_AWAIT_IGNORE_VOID`, never direct
`co_await`/`co_yield`. See `rpc-unit-test` for details.

For a defect or behavior change, use `change-workflow` for RED-GREEN-REFACTOR and completion evidence. Use
`engineering-guidelines` for C++/CMake naming, implementation, and lint rules; do not re-create those policies here.
