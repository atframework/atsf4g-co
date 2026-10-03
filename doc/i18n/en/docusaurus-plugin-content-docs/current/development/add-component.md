---
title: Integrating and Adding Components
---

# Integrating and Adding Components

## Quick Start: Use an Existing Component

1. Choose a [component](../components/overview) and prepare its instances/configuration using its quick start.
2. Add the SDK dependency to the consumer's existing `project_service_declare_instance` declaration.
   For DTMQ, append `dtmq-proxy-sdk` to `USE_COMPONENTS`:
   `USE_COMPONENTS "dtmq-proxy-sdk"`. Friends and matchmaking live under `src/*svr/`;
   put their SDKs in `USE_SERVICE_SDK`.
3. Reconfigure/build, include public SDK headers, and call the API from business actions.
   For notifications, follow declarations in `lobbysvr/service/CMakeLists.txt` and register generated handlers in main.
4. Verify a request and required notifications/callbacks. Ordinary integration needs no edits to component
   CMake helpers, generators, or templates.

Extend the parameters of the **existing declaration**; do not define the service target again.

## Quick Start: Add a Reusable Component

1. For a pure algorithm component, follow SDK layouts under `src/component/GameSharedComponent/`.
   For “protocol + service + SDK”, follow `src/component/dtmq/`.
2. Copy declarations and replace component names, protocol targets, protobuf service full names, output paths,
   and export macros. Declare component protos in `project_component_declare_protocol`'s `PROTOCOLS`;
   reuse `project_component_declare_service` / `project_component_declare_sdk` for services and SDKs.
3. Register the directory in `src/component/CMakeLists.txt`. For a separate process, register its type and
   deployment using [adding a service](add-service). Algorithm-only SDKs need no process configuration.
4. Follow the [protocol](add-protocol) and [RPC](add-rpc-task) quick starts, reconfigure/build, implement the
   behavior, and declare the consuming service's SDK dependency as above.

## Customization and Design

Read `src/component/component-functions.cmake` and [RPC and code generation](../architecture/rpc-codegen)
only when changing linking, generated layouts, or component setup.
Each component's design page covers replication, persistence, and lifecycle rules.
