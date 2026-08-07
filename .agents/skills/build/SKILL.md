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

## Header-change incremental-build pitfall (localized MSVC on a non-UTF-8 system codepage)

Root cause (verified on this tree, `GetACP() == 936`/GBK): ninja records `#deps 0` for every object, so **header
changes — including every PCH input — never trigger recompilation or PCH regeneration**. Mechanism: cl.exe emits the
localized `/showIncludes` note (`注意: 包含文件:`) in GBK under codepage 936. CMake's prefix *detection*
(`CMAKE_DETERMINE_MSVC_SHOWINCLUDES_PREFIX`, run **once** during compiler identification and then cached — see
`CMakeFiles/CMakeConfigureLog.yaml`) captures that; if GBK it is often corrupted (`ef bf bd` = U+FFFD) or written as
UTF-8. At build time cl.exe (no-console under VS Code CMake Tools) again uses `GetACP()` → GBK, whose raw bytes never
equal the stored prefix, so ninja cannot discover header dependencies. Verify with
`ninja -C <BUILD_DIR> -t deps <obj>` (`#deps 0`) and by hex-dumping the `msvc_deps_prefix` line in
`CMakeFiles/rules.ninja` (any `ef bf bd` = corruption, or localized `注意:`).

Fix (verified — `VSLANG=1033`; no system change / no reboot): cl.exe honors `VSLANG` and emits the English
`Note: including file: ` prefix even with no console. Put `"VSLANG": "1033"` in `cmake.environment` (not only
`cmake.configureEnvironment`) so it applies to **both configure and build** — this repo's `.vscode/settings.json`
already does. Then force CMake to **re-run compiler identification** so the cached `/showIncludes` prefix is
re-detected to English: in VS Code run "CMake: Delete Cache and Reconfigure", or delete
`<BUILD_DIR>/CMakeFiles/<cmake-ver>/CMakeCXXCompiler.cmake` and `CMakeCCompiler.cmake` and reconfigure. After that
`msvc_deps_prefix = Note: including file: ` and ninja tracks headers (verified: a `server_frame-protocol-net` PCH then
records `#deps 250`).

What does **not** fix it:
- `LANG`/`LANGUAGE`/`LC_ALL`: POSIX locale variables; Windows cl.exe ignores them entirely (verified — `/showIncludes`
  output stays GBK). They also do nothing useful for ninja, which does not emit the `/showIncludes` text.
- `VSLANG` only in `cmake.configureEnvironment`: covers configure but not the build, so build cl.exe still emits GBK
  and mismatches the (now-English) prefix. Must be in `cmake.environment` (or also `cmake.buildEnvironment`).
- `chcp 65001` / a UTF-8 console alone: does not help — detection is no-console (`GetACP()`), and a fresh
  re-identification is still required.

Alternatives (also work, heavier): set the system ANSI codepage to 65001 (Windows "Beta: Use Unicode UTF-8 for
worldwide language support" → reboot), which makes every cl.exe emit English regardless of `VSLANG`; or a compiler
wrapper that normalizes `/showIncludes` output to English.

After editing any header, force recompilation of the affected translation units, then rebuild. Incremental builds
after editing a header silently keep stale objects and can produce ABI-mismatched binaries that crash at runtime
(observed: `runtime_options` layout change crashing at the first `options` copy with exit code 3). Source-file
(`.cpp`) mtime changes are still tracked normally.

**Do not use `ninja -t clean <target>` for this.** On targets that carry generated outputs (protoc/mako
byproducts, e.g. anything linking a `*-protocol` target or `generate-for-pb` flows) the clean tool also deletes
generated `.pb`/`.pb.h` files and the directory `cmake_install.cmake`; the missing `cmake_install.cmake` then
forces a full build.ninja regen, and the regen-time `generate-for-pb` print step needs `serverframe_all.pb` —
which protoc cannot rebuild whenever the current (possibly dirty) protocol tree does not compile. Recovery then
requires manually rerunning the `src/server_frame/CMakeFiles/serverframe_all.pb-*.bat` custom command before
`cmake .` can succeed again. Instead, delete only the stale objects:
`Remove-Item -Recurse <BUILD_DIR>/<path>/CMakeFiles/<target>.dir/*.obj` (or remove the whole `<target>.dir`
object directory) and rebuild; ninja recompiles those TUs and relinks without triggering a regen. To force a
correct full recompile across the whole tree (e.g. after enabling the UTF-8 system locale, or when many stale PCHs
are suspected), delete only PCH and object artifacts:
`Get-ChildItem -Path <BUILD_DIR> -Recurse -Include *.pch,*.obj | Remove-Item -Force`, then build.

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
