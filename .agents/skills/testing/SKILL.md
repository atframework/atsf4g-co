---
name: testing
description: "Use when: running or writing unit tests, discovering/filtering private test-framework cases, or fixing Windows DLL/PATH test startup issues. For the offline mock-RPC fixture (atframework::testing::runtime, mock SS/DNS/DB engines in src/tools/rpc-unit-test) use rpc-unit-test instead."
---

# Unit testing (atsf4g-co)

This repo uses a **private unit testing framework** (shared by atframe_utils/libatbus/libatapp).

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

## Local etcd for etcd-dependent tests

Some unit tests (`atapp_etcd_cluster`, `atapp_etcd_module`) require a running etcd instance. Use the `setup-etcd` scripts in `atframework/libatapp/ci/etcd/` to download and start a local etcd:

```bash
# Linux / macOS
bash atframework/libatapp/ci/etcd/setup-etcd.sh start
export ATAPP_UNIT_TEST_ETCD_HOST="http://127.0.0.1:12379"
# Run etcd-dependent tests, then:
bash atframework/libatapp/ci/etcd/setup-etcd.sh stop
```

```powershell
# Windows (PowerShell)
.\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command start
$env:ATAPP_UNIT_TEST_ETCD_HOST = "http://127.0.0.1:12379"
# Run etcd-dependent tests, then:
.\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command stop
```

Commands: `download`, `start`, `stop`, `cleanup` (stop + delete all), `status`. Default client port: `12379`.

If `ATAPP_UNIT_TEST_ETCD_HOST` is not set, etcd-dependent tests are skipped (not failed).

## Tip: using VS Code tasks

If the workspace provides CMake build tasks, prefer those to ensure UTF-8 console output and consistent build directories.
