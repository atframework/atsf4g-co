# Service/component CMake declarations

Detail companion to `SKILL.md`. Load when adding or reviewing service protocols, SDK libraries, service executables,
component protocols, component SDKs, or component service executables.

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
- If editing `.proto`, templates, generated code, or generation commands, also read
  [`code-generation.md`](code-generation.md).
- For build/test workflow, load the standalone skills `../build/SKILL.md` and `../testing/SKILL.md`.

## Scope and call order

Top-level flow in `src/CMakeLists.txt`:

1. `src/component` is added first and includes `component-functions.cmake`.
2. Component protocol/SDK/service aliases become available as `components::<name>`.
3. `service-functions.cmake` is included.
4. Normal services (`echosvr`, `cachesvr`, `authsvr`, `lobbysvr`, `rank_settlement_svr`) are added.

Implications:

- Service declarations can depend on component targets via `USE_COMPONENTS`.
- Component declarations should not depend on service targets unless the project structure is intentionally changed.
- Inside a service directory: protocol declarations, then `add_subdirectory(sdk)`, then `add_subdirectory(service)`.
- Inside a component directory: component protocol declarations, optional target customization, then
  `add_subdirectory(sdk)`, then component service subdirectories.

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
- Produces generated `.pb.h`/`.pb.cc` and a descriptor file `service-<TARGET_NAME>.pb`.
- Exports alias: `protocol::<TARGET_NAME>`.

`project_service_declare_sdk(TARGET_NAME SDK_ROOT_DIR ...)`

- Use for service RPC/client SDK libraries.
- Common options: `STATIC`, `SHARED`, `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `DLLEXPORT_DECL`,
  `SHARED_LIBRARY_DECL`, `NATIVE_CODE_DECL`.
- Source lists: `HRADERS <headers...>` and `SOURCES <sources...>`.
- Dependency options:
  - `USE_COMPONENTS <names...>` -> `components::<name>`.
  - `USE_SERVICE_PROTOCOL <names...>` -> `protocol::<name>`.
  - `USE_SERVICE_SDK <names...>` -> `sdk::<name>`.
- Empty `SOURCES` -> `INTERFACE` library; otherwise static/shared per project options.
- Exports alias: `sdk::<TARGET_NAME>`.

`project_service_declare_instance(TARGET_NAME SERVICE_ROOT_DIR ...)`

- Use for normal service executables.
- Common options: `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `RUNTIME_OUTPUT_DIRECTORY`.
- Source/resource lists: `HRADERS`, `SOURCES`, `RESOURCE_DIRECTORIES`, `RESOURCE_FILES`, `PRECOMPILE_HEADERS`.
- Dependency options:
  - `USE_COMPONENTS <names...>` -> `components::<name>`.
  - `USE_SERVICE_SDK <names...>` -> `sdk::<name>`.
  - `USE_SERVICE_PROTOCOL <names...>` -> `protocol::<name>`.
- Executable target name is `TARGET_NAME`; no stable alias is created for normal service instances.
- Default runtime output is `<TARGET_NAME>/bin` when not specified.

### Component helpers

`project_component_declare_protocol(TARGET_NAME PROTOCOL_DIR ...)`

- Use for protobuf protocol/config targets owned by `src/component/**`.
- Required: `PROTOCOLS <proto_files...>`.
- Common options: `OUTPUT_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `DLLEXPORT_DECL`, `OUTPUT_PBFILE_PATH`.
- Dependency option: `USE_COMPONENTS <names...>` -> `components::<name>` and protocol import paths when available.
- Produces generated `.pb.h`/`.pb.cc` and a descriptor file `component-<TARGET_NAME>.pb`.
- Exports alias: `components::<TARGET_NAME>`.

`project_component_declare_sdk(TARGET_NAME SDK_ROOT_DIR ...)`

- Use for reusable component SDK libraries.
- Common options: `STATIC`, `SHARED`, `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `DLLEXPORT_DECL`,
  `SHARED_LIBRARY_DECL`, `NATIVE_CODE_DECL`.
- Source lists: `HRADERS <headers...>` and `SOURCES <sources...>`.
- Dependency option: `USE_COMPONENTS <names...>` -> `components::<name>`.
- Empty `SOURCES` -> `INTERFACE` library; otherwise static/shared per project options.
- Exports alias: `components::<TARGET_NAME>`.

`project_component_declare_service(TARGET_NAME SERVICE_ROOT_DIR ...)`

- Use for component-owned service executables (e.g., rank board, distributed transaction coordinator).
- Common options: `INCLUDE_DIR`, `OUTPUT_NAME`, `OUTPUT_TARGET_NAME`, `RUNTIME_OUTPUT_DIRECTORY`.
- Source/resource lists: `HRADERS`, `SOURCES`, `RESOURCE_DIRECTORIES`, `RESOURCE_FILES`, `PRECOMPILE_HEADERS`.
- Dependency option: `USE_COMPONENTS <names...>` -> `components::<name>`.
- Exports executable alias: `components::<TARGET_NAME>`.
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

Rules:

- Reference dependencies by alias (`protocol::<name>`, `sdk::<name>`, `components::<name>`), not by platform-specific
  real target name.
- Use the bare dependency name in helper arguments, e.g., `USE_SERVICE_SDK "lobbysvr-sdk"` (not
  `USE_SERVICE_SDK sdk::lobbysvr-sdk`).
- Use `OUTPUT_TARGET_NAME SOME_VAR` when later CMake needs the real target (install include dirs, manual links).
- Component protocols, SDKs, and services share the `components::<name>` namespace; choose unique `TARGET_NAME` values.
- Do not hand-write install rules, RPATH, symbol export defs, PCH wiring, or post-build copy hooks unless the helper
  cannot express the requirement.

## Practical patterns

### Service protocol -> SDK -> instance

`src/cachesvr` declares `cachesvr-protocol`, `src/cachesvr/sdk` declares `cachesvr-sdk` with
`USE_SERVICE_PROTOCOL cachesvr-protocol`, `src/cachesvr/service` declares the executable with
`USE_SERVICE_SDK "cachesvr-sdk"`.

### Service config protocol used directly by an executable

`src/authsvr` declares `authsvr-config`; the executable uses `USE_SERVICE_PROTOCOL "authsvr-config"` without an SDK.

### Service consuming component SDKs and service SDKs together

`src/lobbysvr/service` uses `USE_COMPONENTS "rank-logic-sdk"`, `USE_SERVICE_SDK "lobbysvr-sdk"`,
`USE_SERVICE_PROTOCOL "lobbysvr-config"`.

### Component protocol with extra target customization

`src/component/rank/CMakeLists.txt` declares `rank-board-svr-protocol` with `OUTPUT_TARGET_NAME`, then calls
`target_include_directories(${RANK_BOARD_SVR_PROTOCOL} PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")`.

### Component SDK linked to a component protocol

`src/component/distributed_transaction/sdk/CMakeLists.txt` captures `distributed-transaction-sdk` with
`OUTPUT_TARGET_NAME`, then manually links `components::distributed-transaction-protocol`. Prefer `USE_COMPONENTS`;
fall back to `OUTPUT_TARGET_NAME` + `target_link_libraries` only for custom property work after declaration.

### Header-only component SDK

`src/component/test/api/CMakeLists.txt` declares `test-api` with `HRADERS` and no `SOURCES`; the helper exposes
`components::test-api` as `INTERFACE`.

### Component service executable

`src/component/rank/rank_board_svr/CMakeLists.txt` declares `rank-board-svr` with
`USE_COMPONENTS "rank-logic-sdk"` and `"rank-board-config"`.

## Common pitfalls

- `HRADERS` is intentional. `HEADERS` is silently ignored unless the helper is changed.
- `OUTPUT_TARGET_NAME` stores the real target in parent scope; alias names are still preferred for dependencies.
- For protobuf import resolution, declare protocol dependencies via `USE_COMPONENTS` or `USE_SERVICE_PROTOCOL` so the
  helper can extend protoc search paths.
- Helper internals use the misspelled property `PORJECT_PROTOCOL_DIR`. Preserve the spelling unless intentionally
  refactoring.
- Component helper deps all use `USE_COMPONENTS`; no `USE_SERVICE_PROTOCOL`/`USE_SERVICE_SDK` equivalents exist for
  components.
- Normal service executables have no `sdk::`/`protocol::` alias. Depend on their protocols/SDKs, not on the executable
  target, unless explicitly required.
- Avoid broad `file(GLOB_RECURSE ...)` rewrites unless matching local style. Many existing service/component
  executables use it for `SERVICE_SOURCES`/`SERVICE_HEADERS`.

## Validation checklist

- Re-read changed `CMakeLists.txt`; helper argument names must match the implementation.
- Every dependency name must resolve to an alias declared earlier in configure order.
- Protocol changes: `PROTOCOL_DIR` is the import root, `PROTOCOLS` paths live under it.
- SDK/service changes: `INCLUDE_DIR`, `SERVICE_ROOT_DIR`, `SDK_ROOT_DIR` match include paths used by sources.
- For documentation-only edits, run markdown/frontmatter diagnostics and a whitespace check.
- For CMake behavior changes, configure/build via `../build/SKILL.md` and run relevant tests via
  `../testing/SKILL.md`.
