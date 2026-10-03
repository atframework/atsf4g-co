# atsf4g-co

[English](README.md) | [简体中文](README.zh-CN.md)

**atsf4g-co** (AT Service Framework for Game - Coroutine) is a game server framework built on libatbus,
libatapp, libcopp, and other atframework libraries. Project code requires **C++14** and supports
**Windows, Linux, and macOS**.

It supports **C++20 standard coroutines** and **traditional stackful coroutines**, switched with
`PROJECT_SERVER_FRAME_USE_STD_COROUTINE`. Business code using framework coroutine APIs needs no changes.
The C++20 backend needs a standard coroutine toolchain; check third-party version/toolchain requirements separately.

Infrastructure includes atgateway, atproxy, service discovery, Redis access, router caches, and OpenTelemetry.
Reusable components and services include **DTMQ, distributed transactions, leaderboards, friends, matchmaking,
and teams**. **Orbit is a Dedicated Server (DS) management solution for Unreal Engine (UE).**

## Quick Start

Full documentation: [English site](https://atframe.work/en/) · [local sources](doc/i18n/en/docusaurus-plugin-content-docs/current/intro.md).

1. Follow [prerequisites](doc/i18n/en/docusaurus-plugin-content-docs/current/getting-started/prerequisites.md):
   CMake ≥ 3.24, a C++ toolchain, Python 3, and Java Runtime. Windows helper scripts use PowerShell 7+.
2. Build. This example works on Linux/macOS or with a prepared MSVC environment:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

See the [build guide](doc/i18n/en/docusaurus-plugin-content-docs/current/getting-started/build.md) for Windows
Visual Studio generators, optional vcpkg, and coroutine switching.
Reuse an existing workspace's build directory, generator, and toolchain;
this workspace's VS Code configuration uses `build_jobs_cmake_tools`.

3. Set overrides in `install/cloud-native/values/personal/`, build, and check etcd/Redis addresses and process layout.
   Generate instance configuration and start from the publish directory:

```bash
cd build/publish/tools/script
bash start_local_test_env.sh
bash generate_config.sh
cd ../..
bash start_all.sh
```

On Windows use matching `.ps1` scripts via `pwsh -NoLogo -NoProfile -File <script>`.
Omit temporary dependency startup when etcd/Redis are already available externally.
See [run and deploy](doc/i18n/en/docusaurus-plugin-content-docs/current/getting-started/run-deploy.md).

## Routine Development

Existing generation covers RPCs, protocols, databases, and Excel.
Edit declarations and implement business logic. For services/components, copy an existing structure and fill its
declarations; learning CMake helper internals or writing Mako templates is not required.

| Task | Quick start |
| --- | --- |
| RPC / task actions | [Add or modify RPCs](doc/i18n/en/docusaurus-plugin-content-docs/current/development/add-rpc-task.md) |
| Services / components | [Services](doc/i18n/en/docusaurus-plugin-content-docs/current/development/add-service.md), [components](doc/i18n/en/docusaurus-plugin-content-docs/current/development/add-component.md) |
| Excel / server config | [Excel](doc/i18n/en/docusaurus-plugin-content-docs/current/development/excel-config.md), [server config](doc/i18n/en/docusaurus-plugin-content-docs/current/development/server-config.md) |
| Protocols / DB tables | [Protocols](doc/i18n/en/docusaurus-plugin-content-docs/current/development/add-protocol.md), [DB tables](doc/i18n/en/docusaurus-plugin-content-docs/current/development/add-db-table.md) |
| Reusable modules | [Components and module quick starts](doc/i18n/en/docusaurus-plugin-content-docs/current/components/overview.md) |
| Validation | [Unit tests](doc/i18n/en/docusaurus-plugin-content-docs/current/development/testing.md), [offline RPC tests](doc/i18n/en/docusaurus-plugin-content-docs/current/development/rpc-unit-test.md) |

The [development quick start](doc/i18n/en/docusaurus-plugin-content-docs/current/development/overview.md)
collects minimal workflows. Read [architecture](doc/i18n/en/docusaurus-plugin-content-docs/current/architecture/overview.md)
when customizing framework behavior.

## Outputs and Developer Tools

Services and runtime resources go to `<BUILD_DIR>/publish/`; tests default to `<BUILD_DIR>/test/`,
and samples to `<BUILD_DIR>/sample/`.
clangd reads Ninja's `compile_commands.json`; keep VS Code's `--compile-commands-dir` aligned with the build directory.

Unchanged code/resources must retain timestamps. Declare accurate `OUTPUT`, `BYPRODUCTS`, `DEPENDS`, and
`DEPFILE` relationships; use `configure_file` or `cmake -E copy_if_different` for content-stable publication.

See [introduction](doc/i18n/en/docusaurus-plugin-content-docs/current/intro.md) and
[services](doc/i18n/en/docusaurus-plugin-content-docs/current/services/overview.md) for repository layout and service roles.
