# CMake and generated-code rules

Detail companion to `SKILL.md`. Load when editing CMake files, protobuf, templates, or generated code.

## CMake style

- Format CMake with `.cmake-format.yaml`: 120 columns, two-space indentation, Unix line endings.
- Prefer project helper functions over manual target wiring when a helper expresses the relationship.

## Service and component helpers

- Load [`service-functions-cmake.md`](service-functions-cmake.md) before changing service, component, protocol, SDK,
  instance, alias, or helper declarations.
- Keep helper API spelling and aliases exact: source list keyword `HRADERS` is intentional; helper dependency arguments
  use bare names such as `USE_SERVICE_SDK "lobbysvr-sdk"`, not aliased target names.
- Respect declaration order: dependencies must be declared before referenced aliases.

## Protobuf, templates, and generated task/RPC code

- Load [`code-generation.md`](code-generation.md) before editing `.proto`, Mako templates, generated task/RPC code, or
  CMake-driven generation targets.
- Generated outputs should normally be regenerated, not hand-edited.
- If templates emit task/RPC-local protobuf temporaries, also apply [`rpc-protobuf-arena.md`](rpc-protobuf-arena.md).
