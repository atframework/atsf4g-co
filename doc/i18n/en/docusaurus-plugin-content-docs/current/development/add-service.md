---
title: Adding a Service
---

# Adding a Service

## Quick Start: Copy a Standard Service

Use `src/authsvr/` as a small CS RPC service template. For SS RPCs and a separate SDK, follow
`src/lobbysvr/protocol/CMakeLists.txt`. Copy declarations without writing generation templates.
`echosvr` demonstrates raw message echoing and does not include full business RPC setup.

1. Copy `src/authsvr/` to `src/examplesvr/`, keeping `protocol/`, `service/app/`, and business directories.
   Rename the service, main file, config message/header, config target, and export macro. Do not copy
   `*.atfw.gen.*` or the original service's business actions.
2. Declare `ExamplesvrClientService` and its messages in the shared `com.protocol.proto`,
   following the [RPC quick start](add-rpc-task). Change `AuthsvrClientService` to
   `ExamplesvrClientService` in the copied generation declaration.
3. Update the copied main's config section, environment prefix, generated header, and
   `register_handles_for_examplesvrclientservice()`. Keep its shared module/dispatcher initialization
   flow; attach new business modules afterward.
4. Add `add_subdirectory(examplesvr)` to `src/CMakeLists.txt`, reconfigure/build, implement the generated
   actions, and build again.
5. Copy `install/cloud-native/charts/authsvr/` for the new chart and rename its service/config section.
   Assign a new `logic_service_type` in `src/server_frame/config/include/config/extern_service_types.h`;
   set matching `proc_name`, `type_id`, and `type_name` in the new chart's `values.yaml`,
   and add instances to the active values' `non_cloud_native/deploy.yaml`.
6. [Generate configuration and start](../getting-started/run-deploy) the instance; check discovery registration and one normal RPC.

Use distinct service/config names. Type enums, values mappings, and process layout must agree.

## Quick Start: Declare a New SS RPC Service

For a new SS RPC group in an existing standard service, reuse its protocol target and fill the service name and
output locations in the existing helper. For the lobby, append the proto to `lobbysvr-protocol`'s
`PROTOCOLS` and declare:

```cmake
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.ExampleService"
  "${LOBBYSVR_ROOT_DIR}/service"
  TASK_PATH_PREFIX "logic"
  HANDLE_PATH_PREFIX "app"
  PROJECT_NAMESPACE "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR "${LOBBYSVR_ROOT_DIR}/sdk"
  RPC_DLLEXPORT_DECL LOBBY_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS "lobbysvr-protocol"
  INCLUDE_HEADERS "protocol/pbdesc/example_service.pb.h")
```

Add the same service full name to the service and SDK's existing `GENERATED_FLOW_NAMES`, and register
`register_handles_for_exampleservice()` in main. Subsequent methods need only proto and business action edits.
Extend the existing SDK header/source lists for the new API files.

## Customization and Design

Shared service setup is declared in `src/server_frame/logic/logic_server_setup.h`;
see [RPC and code generation](../architecture/rpc-codegen) for generation rules.
Helpers organize service code into an executable and a private static library, with `app/` as the default
entry-point directory. Use project test helpers; see [RPC tests](rpc-unit-test) for service RPC paths.

For custom build, generation, or deployment behavior, read `src/service-functions.cmake`,
`src/tools/generate_for_pb_utility.cmake`, and `install/cloud-native/charts/libapp/`.
