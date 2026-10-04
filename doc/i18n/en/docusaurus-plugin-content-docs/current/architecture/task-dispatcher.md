---
title: Tasks and Dispatching
---

# Tasks and Dispatching (dispatcher / task action)

## Quick Start

1. Declare a method and generate its action using the [RPC quick start](../development/add-rpc-task).
2. Implement `operator()`, await asynchronous results with `RPC_AWAIT_*`, and return with `RPC_RETURN_*`.
3. Reuse standard handler registration/dispatcher initialization; do not register each method again manually.

## Customization and Design

### Three-Layer Structure

```mermaid
flowchart LR
    A[atbus message] --> D[dispatcher_implement<br/>base class: RPC registry/filters]
    D --> TM[task_manager<br/>task creation/scheduling/resume]
    TM --> TA[task_action_*<br/>business coroutine body]
```

- **dispatcher_implement** (`src/server_frame/dispatcher/dispatcher_implement.h`): atapp module base class,
  maintains `rpc_task_action_set_t` (RPC name → task action creator). The registry is populated by the
  Mako-generated `register_handles_for_<service>()`.
- **Three singleton dispatchers**:
  | dispatcher | Handles |
  | --- | --- |
  | `cs_msg_dispatcher` | Client messages upstreamed from atgateway (CS RPC) |
  | `ss_msg_dispatcher` | Inter-service `SSMsg` (SS RPC) |
  | `db_msg_dispatcher` | Redis connections and replies |
- **task_manager** (`dispatcher/task_manager.h`): task creation, timeout management, and a generic start/resume
  generator indexed by `timeout + type + sequence`.

### task action Base Classes

| Base class | Purpose |
| --- | --- |
| `task_action_base` | Base of all actions: `operator()` coroutine body, timeout, trace, result code |
| `task_action_cs_req_base` | Client requests: session validation, response packing |
| `task_action_ss_req_base` | Inter-service requests: `prepare_handle` chain, SS response packing |
| `task_action_ss_rpc_base<Req, Rsp>` | Typed request/response base for generated SS RPC actions |
| `task_action_no_req_base` | Timer/self-driven tasks without a triggering request (e.g. `task_action_auto_save_objects`) |

Generated actions read requests through `get_request_body()` and populate `get_response_body()` inside `operator()`,
then return with `RPC_RETURN_CODE`. See the [RPC quick start](../development/add-rpc-task) for a usable example.

Default rules create business skeletons only when absent; implement `operator()` in them.
Existing skeleton files are preserved as a whole, while generated handlers/APIs are updated.
Synchronize existing skeletons manually after RPC signature changes; marked regions do not migrate them.

### Dual Coroutine Implementations

`task_type_traits.h` (`dispatcher/task_type_traits.h`) provides a unified abstraction over the two backends:

- **C++20 coroutines** (`PROJECT_SERVER_FRAME_USE_STD_COROUTINE=ON`): `copp::generator_future/callable_future`,
  `RPC_AWAIT_TYPE_RESULT(x)` is simply `co_await x`;
- **libcopp cotask**: asynchronous waits suspend/resume stackful coroutines; `rpc_result` stores results in a poller,
  and `rpc_result_guard` wraps return values.

Business code only uses the `RPC_AWAIT_*` / `RPC_RETURN_*` macros and the
`rpc::result_code_type / rpc_result<T>` types, without directly perceiving backend differences.
Await an RPC response with `RPC_AWAIT_CODE_RESULT` or `RPC_AWAIT_TYPE_RESULT`; even when its result is unused,
explicitly await it with `RPC_AWAIT_IGNORE_RESULT`. Macro definitions are in `src/server_frame/rpc/rpc_common_types.h`;
see [awaiting RPC completion](../development/add-rpc-task#await-rpc) for usage.

### Built-in task actions

`src/server_frame/logic/action/` provides framework-level actions: `set_server_time`, `user_logout`,
`reload_remote_server_configure`, `async_invoke`, etc.; `src/server_frame/router/action/` provides router-related
actions (auto save, close, transfer, update_sync).
