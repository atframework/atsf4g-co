---
title: Development Quick Start
---

# Development Quick Start

After [building](../getting-started/build) and [running services](../getting-started/run-deploy), use the table
below for routine development. Each guide starts with minimal steps and then links to customization details.

For existing features, edit proto, Excel, XML, or YAML declarations and let the existing build and generation
workflow handle the output. New business behavior still needs an implementation. For a new service or component,
copy an existing structure and fill in its small build declarations; learning CMake internals or writing Mako
templates is not required.

## Choose a Task

| Task | Minimal change | Guide |
| --- | --- | --- |
| Add or modify an RPC | Edit messages and the rpc declaration, reconfigure/build, implement the generated action | [RPCs and Task Actions](add-rpc-task) |
| Add a service | Copy a standard service, rename it and its protocol declarations, register its directory and deployment | [Adding a Service](add-service) |
| Integrate or add a component | Declare the SDK dependency; copy an existing layout for a new component | [Components](add-component) |
| Change Excel values | Edit an existing workbook, build `resource-config`, publish resources and reload | [Excel Configuration](excel-config) |
| Add an Excel table/field | Declare the config proto, loader indexes, and XML conversion entry, then build | [Excel Configuration](excel-config) |
| Change server configuration | Edit values, generate instance YAML, then reload or restart | [Server Configuration](server-config) |
| Add protocol messages/files | Choose public/private scope, define messages, reconfigure/build | [Adding Protocols](add-protocol) |
| Add a database table | Declare fields and storage options in a table proto, build and use the generated API | [Database Tables](add-db-table) |

## Shared Generation and Build Steps

Reuse an already configured build directory. Replace `<BUILD_DIR>` below with your directory; this workspace's
VS Code configuration uses `build_jobs_cmake_tools`.

```bash
cmake -S . -B <BUILD_DIR>
cmake --build <BUILD_DIR> --config Debug --parallel 4
```

For the initial configuration, choose your toolchain using the [build guide](../getting-started/build).
Reconfiguration keeps the generator, toolchain, and cached options in the existing directory. Replace `Debug`
with your active configuration for multi-config builds. Reconfigure after adding proto, Excel, or business source
files to refresh file lists collected during configuration.

## Editable Files

- Proto, Excel, XML, and values YAML are declaration inputs; business `task_action_*.h/.cpp` files are handwritten implementations.
- Default rules create business skeletons only when absent and leave existing skeletons untouched. When an RPC's
  request/response type changes, update the existing business files too.
- Do not edit `*.atfw.gen.*` or automatic files under `<BUILD_DIR>/_generated/`; edit their inputs and regenerate.

## When to Read the Design

Read [architecture](../architecture/overview) when changing RPC dispatch, wire protocols, storage semantics,
generated file layouts, custom derived Excel data, or deployment template behavior. For normal configuration
changes and extensions to existing RPC services, start with the individual quick starts.
