---
title: Build
---

# Build

## Quick Start

Complete [prerequisites](prerequisites) first. Directory names below are examples; reuse the generator,
toolchain, and options of an existing build directory.

### Windows

Run in PowerShell 7 with the MSVC development environment:

```powershell
cmake -S . -B build_jobs_msvc -G "Visual Studio 17 2022" -A x64
cmake --build build_jobs_msvc --config Debug --parallel 4
```

To use vcpkg, append
`"-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake"` to the initial configure
command. It is an optional dependency source. For a clangd compilation database, use Ninja:

```powershell
cmake -S . -B build_jobs_cmake_tools -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_jobs_cmake_tools --parallel 4
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

Alternatively, use `bash cmake_dev.sh` to select the environment automatically.
Do not change an existing directory's generator. Continue with [run and deploy](run-deploy).

## Switch Coroutine Backends {#coroutine-backend}

Switch the same project with `PROJECT_SERVER_FRAME_USE_STD_COROUTINE`, then reconfigure/build.
Business code using `RPC_AWAIT_*`, `RPC_RETURN_*`, and framework result types needs no changes.

```bash
# Traditional stackful coroutines
cmake -S . -B <BUILD_DIR> -DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=OFF
cmake --build <BUILD_DIR> --config Debug --parallel 4

# C++20 standard coroutines
cmake -S . -B <BUILD_DIR> -DCMAKE_CXX_STANDARD=20 -DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=ON
cmake --build <BUILD_DIR> --config Debug --parallel 4
```

ON depends on successful compiler detection of standard coroutine support; otherwise the option is forced OFF.
The stackful backend supports C++14 project code. To request C++14 for the entire build, configure initially with
`-DCMAKE_CXX_STANDARD=14 -DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=OFF` and select compatible third-party
dependencies. This workspace's C++20 setting chooses a backend rather than defining the minimum requirement.

## Common Build Options

| Option | Default | Purpose |
| --- | --- | --- |
| `PROJECT_ENABLE_SAMPLE` | OFF | Build samples |
| `PROJECT_ENABLE_UNITTEST` | Follows defined `BUILD_TESTING`; otherwise ON in Debug, OFF in other configurations | Build unit tests |
| `PROJECT_ENABLE_PRECOMPILE_HEADERS` | ON | Precompiled headers |
| `PROJECT_ENABLE_UNITY_BUILD` | OFF | Unity build |
| `ATFRAMEWORK_USE_DYNAMIC_LIBRARY` | ON except macOS; follows explicit `BUILD_SHARED_LIBS` | Dynamic libraries |
| `PROJECT_SERVER_FRAME_USE_STD_COROUTINE` | ON when supported, otherwise OFF | Standard/stackful coroutine backend |
| `PROJECT_SERVER_FRAME_ENABLE_RPC_MOCK` | Debug ON | RPC mock |
| `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` | Enabled with unit tests | Offline RPC test hooks |
| `CRYPTO_USE_OPENSSL` / `CRYPTO_USE_MBEDTLS` | Selected by configuration | Crypto backend |

Business services, component services, and ordinary tools are registered in `src/CMakeLists.txt`;
they have no individual service build switches. robot has its own Go build workflow.
For tests, see the [test guide](../development/testing).

## Build Outputs

Services and runtime resources go to `<BUILD_DIR>/publish/`.
Tests default to `<BUILD_DIR>/test/`, controlled by `PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY`;
samples default to `<BUILD_DIR>/sample/`, controlled by `PROJECT_SAMPLE_RUNTIME_OUTPUT_DIRECTORY`.
See [development quick start](../development/overview) for routine generation.

## Customization and Incremental Builds

For custom build behavior, inspect `project/cmake/` and module declarations.
Code/resources consumed by targets must keep timestamps when unchanged.
Declare accurate `OUTPUT`, `BYPRODUCTS`, `DEPENDS`, and `DEPFILE` relationships;
use `configure_file` or `cmake -E copy_if_different` for content-stable publication.

### Regenerate by Target

To update one category of generated artifacts, build its target:

```bash
cmake --build <BUILD_DIR> --config Debug --target protocol
cmake --build <BUILD_DIR> --config Debug --target config-loader
cmake --build <BUILD_DIR> --config Debug --target serverframe_all_pb
```

To rebuild all generated artifacts, use `cleanup-generated-sources`, then build normally.
It removes registered build-time artifacts while keeping configure-time scripts/settings;
do not manually delete the entire `_generated/` directory.

```bash
cmake --build <BUILD_DIR> --config Debug --target cleanup-generated-sources
cmake --build <BUILD_DIR> --config Debug --parallel 4
```
