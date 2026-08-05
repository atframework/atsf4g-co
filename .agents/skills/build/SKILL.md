---
name: build
description: "Use when: configuring or building atsf4g-co with CMake, resolving build errors, or adjusting Windows/MSVC/vcpkg and Unix/Linux/macOS build options."
---

# Build (atsf4g-co)

This project uses **CMake (>= 3.24)** and requires a C++17 toolchain.

## Workspace CMake Settings

Before running `cmake` configure/build/test commands, resolve `<BUILD_DIR>` and reuse workspace settings instead of
guessing:

1. Read `.vscode/settings.json` when it exists.
2. Use `cmake.buildDirectory` when present.
3. If absent, infer from `clangd.arguments` `--compile-commands-dir=...` or an existing configured build tree.
4. If no user setting is readable, use `build`.

In this checkout `.vscode/settings.json` defines `cmake.generator: Ninja`, `CMAKE_BUILD_TYPE: Debug`,
`PROJECT_ENABLE_UNITTEST: ON`, shared-library/dynamic-library options, `CMAKE_CXX_STANDARD: 20`, and
`cmake.parallelJobs: 12`; clangd points at `build_jobs_cmake_tools` for compile commands, so use
`<BUILD_DIR>=build_jobs_cmake_tools` unless the user asks for a different one.

All build trees, AI scratch files, script output/logs, and temporary data must stay under `<BUILD_DIR>/...`; use
`<BUILD_DIR>/_agent_tmp/...` for agent-generated notes, scripts, and logs. Do not create temporary files in the
repository root.

## Header-change incremental-build pitfall (Chinese/localized MSVC)

In this checkout's Windows tree (`build_jobs_cmake_tools`) ninja records `#deps 0` for every object: the
`msvc_deps_prefix` in `CMakeFiles/rules.ninja` does not match the localized cl.exe `/showIncludes` output, so
**header changes never trigger recompilation**. Incremental builds after editing a header silently keep stale
objects and can produce ABI-mismatched binaries that crash at runtime (observed: `runtime_options` layout change
crashing at the first `options` copy with exit code 3). After editing any header, clean the affected targets
before rebuilding, e.g. `ninja -t clean <target...>` (or the whole tree for shared/public headers), then rebuild.
Source-file (`.cpp`) mtime changes are still tracked normally. Verify suspicion with
`ninja -t deps <obj>` showing `#deps 0`.

## Windows (MSVC + vcpkg example)

```bash
# Configure with vcpkg
cmake -S . -B <BUILD_DIR> -G "Visual Studio 17 2022" -A x64 \
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" \
  -DPROJECT_ENABLE_UNITTEST=YES \
  -DPROJECT_ENABLE_SAMPLE=YES \
  -DPROJECT_ENABLE_TOOLS=YES

# Build
cmake --build <BUILD_DIR> --config Debug
cmake --build <BUILD_DIR> --config RelWithDebInfo  # For production
```

Notes:

- This repo often uses a bundled toolset layout; output files are typically under `<BUILD_DIR>/publish/`.

## Unix/Linux/macOS

```bash
# Auto setup with cmake_dev.sh
bash cmake_dev.sh [options] ...

# Example: Enable OpenSSL, unit tests, samples
bash ./cmake_dev.sh -lus -- -DCRYPTO_USE_OPENSSL=YES

# Build
cmake --build <BUILD_DIR> -- -j4
```

## Key CMake Options

| Option                    | Default | Description                |
| ------------------------- | ------- | -------------------------- |
| `PROJECT_ENABLE_UNITTEST` | NO      | Enable unit tests          |
| `PROJECT_ENABLE_SAMPLE`   | NO      | Enable sample applications |
| `PROJECT_ENABLE_TOOLS`    | NO      | Enable tools               |
| `BUILD_SHARED_LIBS`       | NO      | Build shared libraries     |
| `CRYPTO_USE_OPENSSL`      | NO      | Use OpenSSL for crypto     |
| `CRYPTO_USE_MBEDTLS`      | NO      | Use MbedTLS for crypto     |

If you are unsure what was configured, inspect `<BUILD_DIR>/CMakeCache.txt`.
