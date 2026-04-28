---
name: rpc-protobuf-arena
description: >-
  Use when: creating temporary protobuf messages inside task_action classes, RPC APIs, rpc::context callbacks, or
  code-generation templates that emit task/RPC-local messages and should use rpc::make_shared_message with task Arena.
---

# RPC protobuf Arena allocation

Use this skill when a task/RPC implementation creates protobuf message objects that are temporary scratch data.

## Preferred pattern

- If a message is created only inside a task/RPC call and is not cached, saved as a member, or returned beyond the call's
  Arena lifetime, prefer:
  - `rpc::make_shared_message<MessageType>(ctx)` when a `rpc::context &ctx` is available.
  - `rpc::make_shared_message<MessageType>(get_shared_context())` inside `task_action_*` classes.
- Add `#include <rpc/rpc_shared_message.h>` when the symbol is not already included.
- Use the returned shared message naturally:
  - `msg->mutable_field()` / `msg->set_field(...)`
  - `*msg` for APIs such as `PackFrom`, `SerializeToString`, or helpers taking a message reference
  - `msg.get()` for APIs such as `UnpackTo`

## Do not use for long-lived data

Do not store task-Arena messages in members, global structures, caches, delayed callbacks, or data structures that may
outlive the task/RPC context. Copy, clone, or allocate with an owning lifetime that matches the storage instead.

## Example

In a task action, prefer this for a temporary response metadata message:

```cpp
auto cache_meta = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DUserCacheMeta>(get_shared_context());
cache_meta->mutable_user_key()->set_zone_id(user->get_zone_id());
cache_meta->mutable_user_key()->set_user_id(user->get_user_id());

if (!rsp_body.mutable_cache_meta()->mutable_cache_meta()->PackFrom(*cache_meta)) {
  // handle pack error
}
```

This reuses the task/RPC Arena and reduces heap fragmentation compared with creating short-lived stack or heap protobuf
messages.
