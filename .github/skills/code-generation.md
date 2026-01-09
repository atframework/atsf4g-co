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
