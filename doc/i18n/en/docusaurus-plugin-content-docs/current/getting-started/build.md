---
title: Build
---

# Build

## Windows (MSVC + vcpkg)

```powershell
cmake -S . -B build_jobs_msvc -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" `
  -DPROJECT_ENABLE_UNITTEST=YES -DPROJECT_ENABLE_SAMPLE=YES
cmake --build build_jobs_msvc --config Debug
```

If you want to work with clangd, it is recommended to switch to the Ninja generator to produce
`compile_commands.json`, and keep the directory consistent with `.vscode/settings.json`
(`build_jobs_cmake_tools`):

```powershell
cmake -S . -B build_jobs_cmake_tools -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 -DPROJECT_ENABLE_UNITTEST=YES
```

## Linux / macOS

```bash
# Auto-detect and configure (creates build_jobs_$(uname -s))
bash cmake_dev.sh -lus -- -DCRYPTO_USE_OPENSSL=YES

cd build_jobs_*
cmake --build . -- -j4
```

You can also run `cmake -S . -B <BUILD_DIR> [options...]` manually and use your own prebuilt dependency libraries.

## Key CMake Options

| Option | Default | Description |
| --- | --- | --- |
| `PROJECT_ENABLE_SAMPLE` | OFF | Build samples |
| `PROJECT_ENABLE_UNITTEST` | OFF | Build unit tests |
| `PROJECT_ENABLE_PRECOMPILE_HEADERS` | ON | Precompiled headers |
| `PROJECT_ENABLE_UNITY_BUILD` | OFF | Unity build (recommended OFF for local development + clangd) |
| `ATFRAMEWORK_USE_DYNAMIC_LIBRARY` | ON by default on Linux | Build as dynamic libraries |
| `PROJECT_SERVER_FRAME_USE_STD_COROUTINE` | ON after detection | Use C++20 coroutines (otherwise libcopp cotask) |
| `PROJECT_SERVER_FRAME_ENABLE_RPC_MOCK` | ON in Debug | RPC mock |
| `PROJECT_SANTIZER_USE_ADDRESS/THREAD/...` | OFF | Sanitizers |
| `CRYPTO_USE_OPENSSL` / `CRYPTO_USE_MBEDTLS` | — | Crypto backend |

All services (echosvr / cachesvr / authsvr / lobbysvr / rank_settlement_svr / orbitsvr / component services /
tools) are built unconditionally in `src/CMakeLists.txt`; there are no per-service switches.

After the build completes, service executables and runtime resources are located in `<BUILD_DIR>/publish/`; unit-test
executables in `PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY` (default `<BUILD_DIR>/test`) and sample executables in
`PROJECT_SAMPLE_RUNTIME_OUTPUT_DIRECTORY` (default `<BUILD_DIR>/sample`). Both are CMake cache variables and can be
redirected at configure time.

## Incremental Build Conventions

Code and resources consumed by CMake targets must keep their timestamps when content is unchanged: do not
unconditionally touch files or overwrite them with identical content. Generation rules should use accurate
`OUTPUT`/`BYPRODUCTS`/`DEPENDS`/`DEPFILE`; for content-stable publishing use `configure_file` or
`cmake -E copy_if_different`.
