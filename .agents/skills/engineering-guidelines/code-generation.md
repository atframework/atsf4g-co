# Code generation

Detail companion to `SKILL.md`. Load when editing `.proto`, Mako templates, generated task/RPC code, or CMake-driven
generation targets.

## Generation flows

### Protobuf generation

- Source: `*.proto` files in protocol directories.
- Outputs: generated `*.pb.h` / `*.pb.cc`.
- Location: typically under `_generated/` in the build tree.

### Template-based generation

- Templates: `src/templates/`.
- Generated outputs: `_generated/` in the build tree.

## Rules

- Generation is driven by CMake targets; do not hand-edit generated files.
- If `.proto` import paths change, update the corresponding helper arguments per
  [`service-functions-cmake.md`](service-functions-cmake.md).
- When templates or generated task/RPC code create temporary protobuf messages, apply the Arena-backed pattern from
  [`rpc-protobuf-arena.md`](rpc-protobuf-arena.md) instead of stack/heap temporaries that live only within one task/RPC
  call.
- Treat protobuf definitions, Mako templates, and CMake generation rules as the source of truth; regenerate rather than
  hand-edit derived outputs.
