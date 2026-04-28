---
name: code-generation
description: "Use when: editing proto files, Mako templates, generated RPC/task code, protobuf outputs, or CMake-driven code generation."
---

# Code generation (atsf4g-co)

This repository generates code during build in two main ways:

## Protobuf generation

- Source: `*.proto` files in protocol directories
- Outputs: generated `*.pb.h` / `*.pb.cc`
- Location: typically under an internal `_generated/` directory in the build tree

## Template-based generation

- Templates: `src/templates/`
- Generated outputs: `_generated/` directory (in build tree)

Notes:

- Generation is driven by CMake targets; for most workflows you should not edit generated files directly.
- When templates or generated task/RPC code create temporary protobuf messages, prefer the Arena-backed pattern from
  `../rpc-protobuf-arena/SKILL.md` instead of adding stack/heap temporaries that live only within one task/RPC call.
