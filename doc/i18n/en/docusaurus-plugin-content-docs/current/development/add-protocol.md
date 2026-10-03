---
title: Adding Protocols
---

# Adding Protocols

## Quick Start

1. Choose the scope. Put client/server messages under
   `src/server_frame/protocol/public/protocol/pbdesc/` and server-only messages under the matching
   `private/protocol/pbdesc/`. Use `protocol/common/` for shared types and `protocol/config/` for Excel
   and runtime configuration.
2. Add a message to an existing proto or add a proto in those directories. Follow neighboring files for package,
   imports, and `go_package`. Do not reuse field numbers; reserve deleted published field numbers and names.
3. Import the new file where needed, then [reconfigure and build](overview). These root framework directories
   already collect `*.proto` files, so individual messages need no custom generation rules.
4. For a service/component's own `protocol/`, append the new file to the `PROTOCOLS` list in its existing
   `project_service_declare_protocol` or `project_component_declare_protocol` declaration. This is enough
   for ordinary messages; follow the [RPC quick start](add-rpc-task) for a new RPC.
5. Synchronize client protocols and verify requests/responses. A message declaration does not implement business behavior.

## Validation

Check that generated headers contain the new type, consuming services compile, and clients use the same field
definitions. Do not edit generated `.pb.h/.pb.cc` or Go `.pb.go` files.

## Customization and Design

See [RPC and code generation](../architecture/rpc-codegen) for protocol discovery, sandboxing, descriptors, and
generation rules. For atgateway handshakes or wire protocol changes, also read [gateway and proxy](../architecture/gateway-proxy).
