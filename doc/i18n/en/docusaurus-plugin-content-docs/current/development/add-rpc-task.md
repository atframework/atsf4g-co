---
title: Adding RPCs and Task Actions
---

# Adding RPCs and Task Actions

## Quick Start: Add an SS RPC to an Existing Service

For the lobby, edit `src/lobbysvr/protocol/protocol/pbdesc/lobby_service.proto`.
Keep its existing package and imports, add request/response messages, and add the method to `LobbysvrService`:

```protobuf
message SSEchoTextReq { string text = 1; }
message SSEchoTextRsp { string text = 1; }

// Add inside the existing LobbysvrService.
rpc echo_text(SSEchoTextReq) returns (SSEchoTextRsp) {
  option (atframework.rpc_options) = {
    module_name: "action"
    api_name: "EchoText"
    allow_no_wait: false
  };
}
```

[Reconfigure and build](overview); no new template or changes to the existing generation workflow are needed.
The generator creates `src/lobbysvr/service/logic/action/task_action_echo_text.h/.cpp` and updates
registration code and the SDK. Implement the new action's `operator()`:

```cpp
task_action_echo_text::result_type task_action_echo_text::operator()() {
  get_response_body().set_text(get_request_body().text());
  RPC_RETURN_CODE(0);
}
```

Build again. The existing `register_handles_for_lobbysvrservice()` registers the new method.
Callers include the generated `rpc/lobby/lobbysvrservice.atfw.gen.h` and use its `echo_text` API;
use the generated declaration for the exact namespace and target arguments.

## Quick Start: Add a CS RPC to an Existing Service

1. Define client messages under `src/server_frame/protocol/public/protocol/pbdesc/`, following
   `com.protocol.*.proto`, and import them from `com.protocol.proto`.
2. Add the method to the existing `LobbysvrClientService` or `AuthsvrClientService` in
   `com.protocol.proto`. Follow similar RPCs for `module_name`, `api_name`, and request/response types.
3. Reconfigure/build, implement the new `task_action_*`, then rebuild and synchronize client protocols.
   Existing generation declarations and handler registration need no duplicate entries.

`task_action_cs_req_base` provides session and response handling. Existing service declarations enable
`RPC_IGNORE_EMPTY_REQUEST`: define an actual request message for a new upstream call and follow existing
direction conventions. Streaming downstream methods commonly use `google.protobuf.Empty` requests.

## Modifying an Existing RPC

Existing business skeletons are left untouched; generated handlers and caller APIs are updated.
When changing message types, a method name, or `module_name`, inspect the original action's base class,
headers, directory, and call sites, and migrate the business implementation manually. Remove unused business
actions after deleting an RPC. Do not edit `*.atfw.gen.*`.

## Common Declaration Options

| Declaration | Purpose |
| --- | --- |
| `rpc_options.module_name` | Business subdirectory for the action |
| `service_options.module_name` | SDK directory and namespace organization |
| `api_name` | RPC API identifier; the generated C++ function still follows the rpc method name |
| `allow_no_wait: true` | Allows generated no-wait call forms; check the actual generated API |
| `returns (stream X)` | Server-streamed downstream messages, such as `user_dirty_chg_sync` |
| `rpc x(stream Req)` | Request streams, such as `channel_event_sync` |

## Tasks Without Messages

For periodic tasks without an RPC trigger, use the router auto-save task under
`src/server_frame/router/action/` as a reference.
`src/generate-nomsg-task.sh` is a skeleton generation helper; business code still schedules or starts the task.

## Customization and Design

See [adding a service](add-service) for the one-time registration of a new protobuf service.
Read [RPC and code generation](../architecture/rpc-codegen) for custom layouts, filters, or templates,
and [task dispatch](../architecture/task-dispatcher) for scheduling changes.
Use [offline RPC tests](rpc-unit-test) to verify real call paths.
