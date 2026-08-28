---
name: testing
description: "Use when: designing, writing, reviewing, or running generic unit tests, filtering private-framework cases, or fixing Windows test startup/PATH. For offline service RPC tests with atfw::testing::runtime, use rpc-unit-test."
---

# Unit testing (atsf4g-co)

This repository uses a private unit testing framework shared by several atframework projects.

Read [test design and acceptance](references/test-design-and-acceptance.md) when planning, writing, or reviewing cases.
It is not needed merely to run a known test command.

## Discover and run tests (generic)

Most test executables support:

- List test cases: `-l` / `--list-tests`
- Run by group or case: `-r <group>` or `-r <group>.<case>`
- Filter (wildcards): `-f "pattern*"` / `--filter "pattern*"`
- Help/version: `-h`, `-v`

## Windows: DLL lookup via PATH

On Windows, unit tests/samples can fail to start if dependent DLLs are not found.

Preferred approach: **prepend DLL directories to `PATH`** for the current run/debug session.

Typical DLL directories in this repo:

- `<BUILD_DIR>\publish\bin\<Config>` (project DLLs)
- `<REPO_ROOT>\third_party\install\windows-amd64-msvc-19\bin` (third-party DLLs when using the bundled cmake-toolset)

Example (PowerShell):

```powershell
$buildDir = "<BUILD_DIR>"
$cfg = "Debug"
$env:PATH = "$buildDir\publish\bin\$cfg;$buildDir\publish\bin;${PWD}\third_party\install\windows-amd64-msvc-19\bin;" + $env:PATH
Set-Location "$buildDir\_deps\atframe_utils\test\$cfg"
./atframe_utils_unit_test.exe -l
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
