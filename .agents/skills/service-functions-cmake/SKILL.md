---
name: service-functions-cmake
description: "Use when: adding or changing service/component CMake declarations, protobuf protocols, SDK libraries, service instances, components, target aliases, or src/service-functions.cmake and src/component/component-functions.cmake usage."
---

# Service/component CMake declarations (atsf4g-co)

Use this skill when adding or reviewing service protocols, SDK libraries, service executables, component protocols,
component SDKs, or component service executables.

Core helper files:

- `src/service-functions.cmake` — helpers for normal services under `src/*svr`.
- `src/component/component-functions.cmake` — helpers for reusable components and component-owned services under
  `src/component/**`.

## Read before editing

- Inspect the closest existing `CMakeLists.txt` in the same service/component family and mirror its pattern.
- Check declaration order. Dependencies must be declared before they are referenced by alias.
- Use the current helper API spelling exactly: the list argument is `HRADERS`, not `HEADERS`.
- Prefer helper arguments (`USE_COMPONENTS`, `USE_SERVICE_PROTOCOL`, `USE_SERVICE_SDK`) over manual
  `target_link_libraries` when the relationship is expressible by the helper.
- Use `OUTPUT_TARGET_NAME` only when extra target customization is needed after declaration.
- If editing `.proto`, templates, generated code, or generation commands, also read `code-generation/SKILL.md`.
- If configuring/building or fixing build failures, also read `build/SKILL.md`; for tests, read `testing/SKILL.md`.

## Scope and call order

Top-level flow in `src/CMakeLists.txt`:

1. `src/component` is added first and includes `component-functions.cmake`.
2. Component protocol/SDK/service aliases become available as `components::<name>`.
3. `service-functions.cmake` is included.
4. Normal services (`echosvr`, `cachesvr`, `authsvr`, `lobbysvr`, `rank_settlement_svr`) are added.

Implications:

- Service declarations can depend on component targets via `USE_COMPONENTS`.
- Component declarations should not depend on service targets unless the project structure is intentionally changed.
- Inside a service directory, the common order is protocol declarations, then `add_subdirectory(sdk)`, then
  `add_subdirectory(service)`.
- Inside a component directory, the common order is component protocol declarations, optional target customization,
  then `add_subdirectory(sdk)`, then component service subdirectories.

## Helper reference

### Service helpers

`project_service_declare_protocol(TARGET_NAME PROTOCOL_DIR ...)`

- Use for protobuf protocol/config targets owned by a normal service.
- Required: `PROTOCOLS <proto_files...>`.
- Common options: `OUTPUT_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `DLLEXPORT_DECL`, `OUTPUT_PBFILE_PATH`.
- Dependency options:
  - `USE_COMPONENTS <names...>` links `components::<name>` and adds the dependency protocol directory to protoc search
    paths when available.
  - `USE_SERVICE_PROTOCOL <names...>` links `protocol::<name>` and adds its protocol directory to protoc search paths.
- Produces generated `.pb.h`/`.pb.cc` files and a descriptor file named `service-<TARGET_NAME>.pb`.
- Exports an alias target: `protocol::<TARGET_NAME>`.

`project_service_declare_sdk(TARGET_NAME SDK_ROOT_DIR ...)`

- Use for service RPC/client SDK libraries.
- Common options: `STATIC`, `SHARED`, `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `DLLEXPORT_DECL`,
  `SHARED_LIBRARY_DECL`, `NATIVE_CODE_DECL`.
- Source lists: `HRADERS <headers...>` and `SOURCES <sources...>`.
- Dependency options:
  - `USE_COMPONENTS <names...>` -> `components::<name>`.
  - `USE_SERVICE_PROTOCOL <names...>` -> `protocol::<name>`.
  - `USE_SERVICE_SDK <names...>` -> `sdk::<name>`.
- If `SOURCES` is empty, the helper creates an `INTERFACE` library; otherwise it creates static/shared library according
  to project options.
- Exports an alias target: `sdk::<TARGET_NAME>`.

`project_service_declare_instance(TARGET_NAME SERVICE_ROOT_DIR ...)`

- Use for normal service executables.
- Common options: `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `RUNTIME_OUTPUT_DIRECTORY`.
- Source/resource lists: `HRADERS`, `SOURCES`, `RESOURCE_DIRECTORIES`, `RESOURCE_FILES`, `PRECOMPILE_HEADERS`.
- Dependency options:
  - `USE_COMPONENTS <names...>` -> `components::<name>`.
  - `USE_SERVICE_SDK <names...>` -> `sdk::<name>`.
  - `USE_SERVICE_PROTOCOL <names...>` -> `protocol::<name>`.
- The executable target name is `TARGET_NAME`; no stable alias is created for normal service instances.
- Default runtime output is `<TARGET_NAME>/bin` when not specified.

### Component helpers

`project_component_declare_protocol(TARGET_NAME PROTOCOL_DIR ...)`

- Use for protobuf protocol/config targets owned by `src/component/**`.
- Required: `PROTOCOLS <proto_files...>`.
- Common options: `OUTPUT_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `DLLEXPORT_DECL`, `OUTPUT_PBFILE_PATH`.
- Dependency option: `USE_COMPONENTS <names...>` -> `components::<name>` and protocol import paths when available.
- Produces generated `.pb.h`/`.pb.cc` files and a descriptor file named `component-<TARGET_NAME>.pb`.
- Exports an alias target: `components::<TARGET_NAME>`.

`project_component_declare_sdk(TARGET_NAME SDK_ROOT_DIR ...)`

- Use for reusable component SDK libraries.
- Common options: `STATIC`, `SHARED`, `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `DLLEXPORT_DECL`,
  `SHARED_LIBRARY_DECL`, `NATIVE_CODE_DECL`.
- Source lists: `HRADERS <headers...>` and `SOURCES <sources...>`.
- Dependency option: `USE_COMPONENTS <names...>` -> `components::<name>`.
- If `SOURCES` is empty, the helper creates an `INTERFACE` library; otherwise it creates static/shared library according
  to project options.
- Exports an alias target: `components::<TARGET_NAME>`.

`project_component_declare_service(TARGET_NAME SERVICE_ROOT_DIR ...)`

- Use for component-owned service executables such as rank board or distributed transaction coordinator services.
- Common options: `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `RUNTIME_OUTPUT_DIRECTORY`.
- Source/resource lists: `HRADERS`, `SOURCES`, `RESOURCE_DIRECTORIES`, `RESOURCE_FILES`, `PRECOMPILE_HEADERS`.
- Dependency option: `USE_COMPONENTS <names...>` -> `components::<name>` for component protocols, SDKs, and other
  component aliases.
- Exports an executable alias target: `components::<TARGET_NAME>`.
- Default runtime output is `component/<TARGET_NAME>/bin` when not specified.

## Target aliases and dependency rules

| Declaration helper                   | Real target name                                                | Public alias         | Dependency arguments                                        | Link visibility         |
| ------------------------------------ | --------------------------------------------------------------- | -------------------- | ----------------------------------------------------------- | ----------------------- |
| `project_service_declare_protocol`   | Windows: `pp-<name>`; other: `${PROJECT_NAME}-protocol-<name>`  | `protocol::<name>`   | `USE_COMPONENTS`, `USE_SERVICE_PROTOCOL`                    | `PUBLIC`                |
| `project_service_declare_sdk`        | Windows: `ps-<name>`; other: `${PROJECT_NAME}-service-<name>`   | `sdk::<name>`        | `USE_COMPONENTS`, `USE_SERVICE_PROTOCOL`, `USE_SERVICE_SDK` | `PUBLIC` or `INTERFACE` |
| `project_service_declare_instance`   | `<name>`                                                        | none                 | `USE_COMPONENTS`, `USE_SERVICE_PROTOCOL`, `USE_SERVICE_SDK` | `PRIVATE`               |
| `project_component_declare_protocol` | Windows: `pc-<name>`; other: `${PROJECT_NAME}-component-<name>` | `components::<name>` | `USE_COMPONENTS`                                            | `PUBLIC`                |
| `project_component_declare_sdk`      | Windows: `pc-<name>`; other: `${PROJECT_NAME}-component-<name>` | `components::<name>` | `USE_COMPONENTS`                                            | `PUBLIC` or `INTERFACE` |
| `project_component_declare_service`  | Windows: `pc-<name>`; other: `${PROJECT_NAME}-component-<name>` | `components::<name>` | `USE_COMPONENTS`                                            | `PRIVATE`               |

Rules for agents:

- Reference dependencies by alias (`protocol::<name>`, `sdk::<name>`, `components::<name>`), not by platform-specific real
  target name.
- Use the bare dependency name in helper arguments, for example `USE_SERVICE_SDK "lobbysvr-sdk"`, not
  `USE_SERVICE_SDK sdk::lobbysvr-sdk`.
- Use `OUTPUT_TARGET_NAME SOME_VAR` when later CMake needs the real target, for example to add install include dirs or
  manual links.
- Component protocols, component SDKs, and component services share the `components::<name>` alias namespace; choose
  unique `TARGET_NAME` values.
- Do not hand-write install rules, RPATH setup, symbol export definitions, precompiled-header wiring, or post-build copy
  hooks unless the helper cannot express the requirement.

## Practical patterns from this repository

### Service protocol -> service SDK -> service instance

`src/cachesvr` declares `cachesvr-protocol`, then `src/cachesvr/sdk` declares `cachesvr-sdk` with
`USE_SERVICE_PROTOCOL cachesvr-protocol`, then `src/cachesvr/service` declares the executable with
`USE_SERVICE_SDK "cachesvr-sdk"`.

Use this pattern when a service exposes RPC APIs consumed by other services.

### Service config protocol used directly by an executable

`src/authsvr` declares `authsvr-config` and its service executable uses `USE_SERVICE_PROTOCOL "authsvr-config"` without
an SDK target.

Use this pattern for service-local config protobufs or protocol-only dependencies.

### Service consuming component SDKs and service SDKs together

`src/lobbysvr/service` uses `USE_COMPONENTS "rank-logic-sdk"`, `USE_SERVICE_SDK "lobbysvr-sdk"`, and
`USE_SERVICE_PROTOCOL "lobbysvr-config"`.

Use this pattern for business services that combine reusable component logic with service-owned RPC/config protocols.

### Component protocol with extra target customization

`src/component/rank/CMakeLists.txt` declares `rank-board-svr-protocol` with `OUTPUT_TARGET_NAME`, then calls
`target_include_directories(${RANK_BOARD_SVR_PROTOCOL} PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")`.

Use this pattern only when the helper-created target needs extra properties that are not covered by helper arguments.

### Component SDK linked to a component protocol

`src/component/distributed_transaction/sdk/CMakeLists.txt` captures `distributed-transaction-sdk` with
`OUTPUT_TARGET_NAME`, then manually links `components::distributed-transaction-protocol`.

Prefer `USE_COMPONENTS` for simple component dependencies; use `OUTPUT_TARGET_NAME` plus `target_link_libraries` when
you must apply a custom link or property after declaration.

### Header-only component SDK

`src/component/test/api/CMakeLists.txt` declares `test-api` with `HRADERS` and no `SOURCES`, so the helper creates an
`INTERFACE` target exposed as `components::test-api`.

Use this pattern for component APIs that only publish headers.

### Component service executable

`src/component/rank/rank_board_svr/CMakeLists.txt` declares `rank-board-svr` with `USE_COMPONENTS "rank-logic-sdk"` and
`"rank-board-config"`.

Use this pattern for component-owned processes that should live under the component build/export/runtime layout.

## Common pitfalls

- `HRADERS` is intentionally the current parsed argument name. `HEADERS` will be ignored unless the helper
  implementation is changed.
- `OUTPUT_TARGET_NAME` stores the real target in the parent scope. Use it for CMake target operations; keep alias names
  for dependencies.
- For protobuf import resolution, declare protocol dependencies via `USE_COMPONENTS` or `USE_SERVICE_PROTOCOL` so helper
  code can add the dependency protocol directory to protoc search paths.
- The helper internals currently use the misspelled target property `PORJECT_PROTOCOL_DIR`. Preserve that spelling when
  working inside helper implementation unless doing an intentional compatibility refactor.
- Component helper dependencies all use `USE_COMPONENTS`; there are no separate component equivalents of
  `USE_SERVICE_PROTOCOL` or `USE_SERVICE_SDK`.
- Normal service executables do not create `sdk::` or `protocol::` aliases. Only depend on their protocols/SDKs, not on
  service executable targets, unless a task explicitly requires executable-level target wiring.
- Avoid broad `file(GLOB_RECURSE ...)` rewrites unless matching existing local style. Many current service/component
  executable declarations use it for `SERVICE_SOURCES` and `SERVICE_HEADERS`.

## Validation checklist

- Re-read the changed `CMakeLists.txt` and ensure helper argument names match the implementation.
- Confirm every dependency name resolves to an alias declared earlier in the configure order.
- For protocol changes, ensure `PROTOCOL_DIR` is the import root and `PROTOCOLS` paths are under that root.
- For SDK/service changes, ensure `INCLUDE_DIR`, `SERVICE_ROOT_DIR`, and `SDK_ROOT_DIR` match include paths used by
  source files.
- For documentation-only skill edits, run markdown/frontmatter diagnostics and a whitespace check.
- For CMake behavior changes, configure/build with the repository build workflow and run relevant tests.
