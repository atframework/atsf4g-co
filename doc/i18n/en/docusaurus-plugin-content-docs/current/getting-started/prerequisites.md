---
title: Prerequisites
---

# Prerequisites

## Platforms and Language Requirements

Windows, Linux, and macOS are supported. Project code requires **C++14**; traditional stackful coroutines do
not require C++20. The standard coroutine backend requires C++20 and standard coroutine support;
see [build](build#coroutine-backend) for switching.

Third-party dependencies have their own compiler and language requirements; newer releases can need higher
standards. The build chooses dependency versions and available features for the toolchain. A local C++20 build
setting does not define the project's minimum source requirement. Setting `CMAKE_CXX_STANDARD=14` alone
does not make arbitrary dependency versions C++14-compatible.

## Toolchain

| Tool | Purpose and requirement |
| --- | --- |
| CMake ≥ 3.24 | Configuration, code generation, and builds |
| MSVC / GCC / Clang / AppleClang | C++14 support; the standard coroutine backend also needs C++20 coroutine support |
| Python 3 | Protocol/config generation; dependencies are configured by the project toolchain |
| Java Runtime | xresloader Excel exports; checked during configuration |
| PowerShell 7+ | Windows build and runtime helper scripts |
| Bash | Linux/macOS build and runtime helper scripts |
| git | Project and framework submodules |
| Go | robot simulated client; version/build managed by robot tooling |
| vcpkg (optional) | Pass its toolchain file when using existing Windows dependencies |

cmake-toolset ports manage dependencies (`third_party/Repository.cmake`); existing dependency installations
can also be used. After the [initial build configuration](build), reuse the directory for routine development.

## Runtime Dependencies

### etcd

Standard services register/discover through etcd; atproxy uses it for online detection too.
Published `tools/script/start_local_test_env.sh/.ps1` scripts start etcd and Redis together;
see [run and deploy](run-deploy).

For a temporary etcd-only test instance:

```bash
bash atframework/libatapp/ci/etcd/setup-etcd.sh start
```

```powershell
pwsh -NoLogo -NoProfile -File atframework/libatapp/ci/etcd/setup-etcd.ps1 -Command start
```

The default port is 12379. `ATAPP_UNIT_TEST_ETCD_HOST` belongs to libatapp tests.
Configure business services' etcd addresses in deployment values; the two settings are not synchronized automatically.

### Redis

Services using persistence for user data, caches, DTMQ, transactions, and similar features need Redis.
The framework supports cluster and raw/sentinel connections; configure them for the selected services.
The raw message echo example does not require the complete data layer.

## Directory Conventions

`<BUILD_DIR>` means your actual build directory. Prefer `cmake.buildDirectory` in `.vscode/settings.json`;
otherwise use clangd's `--compile-commands-dir` or an existing directory. This workspace uses
`build_jobs_cmake_tools`.

Services and runtime resources go to `<BUILD_DIR>/publish/`; unit tests default to `<BUILD_DIR>/test/`,
and samples to `<BUILD_DIR>/sample/`. Linux/macOS's `cmake_dev.sh` can also create a platform-specific build directory.
