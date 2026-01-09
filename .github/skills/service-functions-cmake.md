# CMake service helper functions (atsf4g-co)

Helper CMake functions are defined in `src/service-functions.cmake`.

## Declare a service protocol (protobuf)

`project_service_declare_protocol(...)` declares protocol targets and generation.

Common parameters:

- `TARGET_NAME`
- `PROTOCOL_DIR`
- `PROTOCOLS <proto_files...>`
- `USE_COMPONENTS <components...>`
- `USE_SERVICE_PROTOCOL <protocols...>`
- `OUTPUT_DIR <dir>`
- `DLLEXPORT_DECL <decl>`

## Declare a service SDK

`project_service_declare_sdk(...)` declares SDK library targets.

Common parameters:

- `TARGET_NAME`
- `SDK_ROOT_DIR`
- `SOURCES <source_files...>`
- `HEADERS <header_files...>`
- `USE_COMPONENTS <components...>`
- `USE_SERVICE_PROTOCOL <protocols...>`
- `USE_SERVICE_SDK <sdks...>`

## Target aliases

- `protocol::<name>` - Protocol library target
- `sdk::<name>` - SDK library target
- `components::<name>` - Component library target
