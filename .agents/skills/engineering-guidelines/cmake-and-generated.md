# CMake and generated-code rules

Detail companion to `SKILL.md`. Load when editing CMake files, protobuf, templates, or generated code.

## CMake style

- Every main-project CMake script (`CMakeLists.txt`, `*.cmake`; excluding vendored `atframework/**` and
  `third_party/**`) must begin with the standard two-line copyright header followed by one blank line:

  ```cmake
  # Copyright <year> atframework
  # Licensed under the Apache License, Version 2.0 (the "License");
  ```

  It must be the first thing in the file (`.cmake-format.yaml` sets `first_comment_is_literal: True` so it is never
  reflowed), before `cmake_minimum_required` or `include_guard`. Use the current year when creating a file; do not
  rewrite years in existing headers.
- Format CMake with `.cmake-format.yaml`: 120 columns, two-space indentation, Unix line endings.
- Prefer project helper functions over manual target wiring when a helper expresses the relationship.
- Target names must never hardcode the project name prefix: write `${PROJECT_NAME}-<name>` (or the `atframework`
  prefix where that is the established namespace of the component), never literal `atf4g-co-...`/`atf4g-co::...`.
  This applies to `add_library`/`add_executable`/`add_custom_target`, ALIAS targets, `if(TARGET ...)` checks,
  `add_dependencies`, `$<TARGET_OBJECTS:...>`, folder/label strings, and diagnostic messages.

## Unit-test targets

- Add every ordinary C++ unit-test target in this root project through `project_add_normal_unit_test` from
  `project/cmake/ProjectTestTools.cmake`.
- Under `src/**`, use `project_add_rpc_unit_test` instead when any case in the executable exercises project RPC paths or
  requires async test hooks guarded by `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`. A target that mixes ordinary and
  RPC/hook cases is an RPC unit-test target.
- In consuming test `CMakeLists.txt` files, do not call `add_executable`, `atframe_add_test_executable`, or `add_test`
  directly and do not reproduce their link, dependency, PCH, post-build, RPATH, runtime-environment, or label setup.
  Pass sources, libraries, features, environment, labels, and timeout through the selected project helper.
- `atframe_add_test_executable` is an implementation detail of `project_add_normal_unit_test`, not a root-project test
  target API. Vendored `atframework/**` subprojects follow their own nearest `AGENTS.md` and CMake conventions.

## Incremental build stability

- Never unconditionally `touch` or overwrite unchanged code, headers, protocols, configuration, or resources consumed
  by `add_custom_command`, `add_custom_target`, `add_executable`, `add_library`, `target_sources`, or another dependency
  edge. This includes generated, copied, and non-handwritten files.
- A file-producing rule belongs in `add_custom_command(OUTPUT ...)`. Declare primary outputs with `OUTPUT`, secondary
  outputs with `BYPRODUCTS`, actual inputs and generator targets with `DEPENDS`, and dynamic dependencies with `DEPFILE`
  when supported. Let `add_custom_target` depend on those outputs instead of regenerating files unconditionally.
- At configure/generate time, prefer content-stable operations such as `configure_file`, `file(CONFIGURE)`, and
  `file(GENERATE)`. At build time, require the generator to preserve unchanged outputs or generate to a temporary path
  and publish with `cmake -E copy_if_different`.
- Touching is allowed only for a dedicated stamp or witness whose timestamp intentionally represents command completion.
  It must not be compiled, linked, packaged, installed as a runtime resource, or used instead of declaring real
  outputs/byproducts.

## Service and component helpers

- Load [`service-functions-cmake.md`](service-functions-cmake.md) before changing service, component, protocol, SDK,
  instance, alias, or helper declarations.
- Keep helper API spelling and aliases exact: source list keyword is `HEADERS`; helper dependency arguments
  use bare names such as `USE_SERVICE_SDK "lobbysvr-sdk"`, not aliased target names.
- Respect declaration order: dependencies must be declared before referenced aliases.

## Protobuf, templates, and generated task/RPC code

- Load [`code-generation.md`](code-generation.md) before editing `.proto`, Mako templates, generated task/RPC code, or
  CMake-driven generation targets.
- Generated outputs should normally be regenerated, not hand-edited.
- If templates emit task/RPC-local protobuf temporaries, also apply [`rpc-protobuf-arena.md`](rpc-protobuf-arena.md).
