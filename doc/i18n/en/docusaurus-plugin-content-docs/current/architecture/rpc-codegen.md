---
title: RPC and Code Generation
---

# RPC and Code Generation

## Protocol as the Source of Truth

All protocols are defined in protobuf, rooted at `src/server_frame/protocol/`:

```
protocol/
├── private/protocol/    # Server-internal only
│   ├── pbdesc/          # svr.protocol.proto (RouterService/LogicCommonService), svr.*.table.proto (DB tables)
│   ├── common/  config/  extension/  log/
└── public/protocol/     # Shareable with clients
    ├── extension/atframework.proto   # Custom service/rpc options (drive code generation)
    └── pbdesc/ com.protocol*.proto (CS/SS messages), com.struct*.proto (data structures)
```

**All generated artifacts are regenerated, never edited by hand** (`*.atfw.gen.{h,cpp}`, `*.pb.{h,cc}`,
config/db code).

## Custom Options

The extension options that drive code generation are spread across three files:

- `public/protocol/extension/atframework.proto`: `atframework.service_options` (`module_name`, etc.) and
  `atframework.rpc_options` (`api_name`, `allow_no_wait`, etc.);
- `protocol/extension/xrescode_extensions_v3.proto`: the `xrescode.loader` option, marking Excel config loaders;
- `private/protocol/extension/svr.database.extension.proto`: DB table/index extensions (KV/KL/CAS/TTL), used by
  the db templates.

## Generation Pipeline

```mermaid
flowchart LR
    P["*.proto"] -->|protoc| PB["*.pb.h/.cc + pb descriptor set"]
    PB -->|generate-for-pb<br/>mako-generator.py| G["*.atfw.gen.h/.cpp"]
    T["src/templates/*.mako"] --> G
```

- `src/server_frame/generate_proto_source.cmake` / `generate_proto_utility.cmake`: protoc compilation;
- `src/tools/generate-for-pb/mako-generator.py`: reads the pb descriptor set and renders in bulk according to the
  rules declared in each CMakeLists (service name + template + output path);
- `src/tools/generate_for_pb_utility.cmake`: CMake-side wrapper for invoking templates.

## Template Inventory (src/templates/)

| Template | Generated artifact |
| --- | --- |
| `handle_ss_rpc.*.mako` | SS RPC registration function `register_handles_for_<service>` |
| `task_action_ss_rpc.*.mako` | Server-side task action skeleton for each SS RPC method |
| `rpc_call_api_for_ss.*.mako` | SS RPC client call APIs (unary/stream/no-wait/broadcast/metadata/user/router variants) and per-RPC full-name accessors |
| `handle_cs_rpc.*.mako` / `task_action_cs_rpc.*.mako` | Handlers and task actions for client RPCs |
| `session_downstream_api_for_cs.*.mako` | Server→client session downstream push APIs and per-RPC full-name accessors |
| `package_request_api_for_simulator.*.mako` | CS request packing APIs for the robot/simulator |
| `task_action_no_msg.*.mako` | No-message task actions (used with `src/generate-nomsg-task.sh`) |
| `db_interface.*.mako` / `db_rpc_redis(.kv/.kl).*.mako` | Redis database access layer (`rpc/db/local_db_interface.atfw.gen.*`) |
| `config_manager.*.mako` / `config_set.*.mako` / `config_easy_api.*.mako` | Excel config loading framework and convenient read APIs |

The orbit component has its own dedicated templates: `src/component/orbit/sdk/server/template/` (they also
generate per-RPC full-name accessors).

The SS/CS/orbit templates all generate `gsl::string_view get_full_name_of_<rpc>()` inside the `<service>` namespace
(declared in `<service>.atfw.gen.h`), returning the on-wire full RPC name (`package.Service/method` for SS/CS; the
orbit fork uses its dotted protocol name). When registering SS mocks or asserting call history via `test.ss()`, use the
SS/CS-template accessor instead of a hardcoded string (see [RPC unit testing](../development/rpc-unit-test.md)); the
orbit-fork getter returns its dotted name and orbit RPCs traverse orbit transport (not the SS engine), so it must not
be passed to `test.ss()`.

## RPC Caller API Shape

Generated callers return awaitable objects:

```cpp
// unary: suspends to wait for the response
auto res = RPC_AWAIT_CODE_RESULT(rpc::SomeService::some_rpc(ctx, req, ...));

// no-wait (rpc_options.allow_no_wait): send without waiting
// stream: server-side streaming push (e.g. dtmq's channel_event_sync)
```

Error handling convention: framework-level failures go through `rpc::result_code_type` (including HTTP-style
error codes and PB unpacking errors); business error codes are defined in `svr.const.err.proto` /
`com.const.proto`.
