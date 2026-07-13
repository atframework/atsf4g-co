# CMake and generated-code rules

Detail companion to `SKILL.md`. Load when editing CMake files, protobuf, templates, or generated code.

## CMake style

- Format CMake with `.cmake-format.yaml`: 120 columns, two-space indentation, Unix line endings.
- Prefer project helper functions over manual target wiring when a helper expresses the relationship.

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
- Keep helper API spelling and aliases exact: source list keyword `HRADERS` is intentional; helper dependency arguments
  use bare names such as `USE_SERVICE_SDK "lobbysvr-sdk"`, not aliased target names.
- Respect declaration order: dependencies must be declared before referenced aliases.

## Protobuf, templates, and generated task/RPC code

- Load [`code-generation.md`](code-generation.md) before editing `.proto`, Mako templates, generated task/RPC code, or
  CMake-driven generation targets.
- Generated outputs should normally be regenerated, not hand-edited.
- If templates emit task/RPC-local protobuf temporaries, also apply [`rpc-protobuf-arena.md`](rpc-protobuf-arena.md).
