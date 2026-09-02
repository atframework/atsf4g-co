---
name: build
description: "Use when: configuring or building atsf4g-co with CMake, or diagnosing configure, compile, or link failures. Do not use merely because a code edit may later need verification."
---

# Build (atsf4g-co)

This project uses CMake >= 3.24 and requires a C++17-capable toolchain.

## Workspace CMake Settings

Before running `cmake` configure/build/test commands, resolve `<BUILD_DIR>` and reuse workspace settings instead of
guessing:

1. Read `.vscode/settings.json` when it exists.
2. Use `cmake.buildDirectory` when present.
3. If absent, infer from `clangd.arguments` `--compile-commands-dir=...` or an existing configured build tree.
4. If no user setting is readable, use `build`.

All build trees, AI scratch files, script output/logs, and temporary data must stay under `<BUILD_DIR>/...`; use
`<BUILD_DIR>/_agent_tmp/...` for agent-generated notes, scripts, and logs. Do not create temporary files in the
repository root. Read the live workspace settings on every run instead of copying their current values into this Skill.

## Configure and build

- Reuse an existing configured tree when it matches the requested toolchain and options.
- For a new tree, pass the generator and configure settings resolved from the workspace; do not silently replace
  explicit user options with defaults.
- Build with `cmake --build <BUILD_DIR> --parallel <jobs>` and add `--target <target>` for the narrowest useful target.
- Inspect `<BUILD_DIR>/CMakeCache.txt` rather than guessing what was configured. Server/tool outputs go to
  `<BUILD_DIR>/publish/`; unit-test executables to `PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY` (default `<BUILD_DIR>/test`)
  and samples to `PROJECT_SAMPLE_RUNTIME_OUTPUT_DIRECTORY` (default `<BUILD_DIR>/sample`); all three are redirectable
  cache variables.
- Read [platform configure examples](references/platform-configure.md) only when creating a new Windows/vcpkg or
  Unix-like build tree, or when choosing project feature options.

## Windows/Ninja header dependencies

If a header edit does not rebuild affected objects, `ninja -t deps` reports `#deps 0`, or a rebuilt binary appears ABI
stale, read [the MSVC header-dependency diagnostic](references/windows-msvc-header-dependencies.md) before cleaning or
reconfiguring. Never use `ninja -t clean <target>` as the first recovery step on targets with generated outputs.

## Validation

- Read the full configure/build output and exit code; a wrapper exit alone does not prove its inner command ran.
- After fixing a build failure, rerun the narrow failed target, then proportionate dependent targets.
- Report any dependency download, toolchain, permissions, or environment boundary that prevented a complete build.
