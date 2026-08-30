# RPC protobuf Arena allocation

Detail companion to `SKILL.md`. Load when a task/RPC implementation creates protobuf message objects that are temporary
scratch data.

## Preferred pattern

- If a message is created only inside a task/RPC call and is not cached, saved as a member, or returned beyond the
  call's Arena lifetime, prefer:
  - `rpc::make_shared_message<MessageType>(ctx)` when a `rpc::context &ctx` is available.
  - `rpc::make_shared_message<MessageType>(get_shared_context())` inside `task_action_*` classes.
- Add `#include <rpc/rpc_shared_message.h>` when the symbol is not already included.
- Pass `*msg` to APIs taking a message reference. Use `msg.get()` only when an API requires a raw pointer.

## Temporary message example

`MessageType`, `field`, and `payload` are placeholders. Replace them with current generated types and fields.

```cpp
auto message = rpc::make_shared_message<MessageType>(ctx);
message->set_field(value);

if (!response.mutable_payload()->PackFrom(*message)) {
  // Use the surrounding function's existing conversion-error path.
}
```

## Do not use for long-lived data

Do not store task-Arena messages in members, global structures, caches, delayed callbacks, or data structures that may
outlive the task/RPC context. Copy, clone, or allocate with an owning lifetime that matches the storage instead.
