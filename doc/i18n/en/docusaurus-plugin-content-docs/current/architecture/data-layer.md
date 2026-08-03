---
title: Data Layer
---

# Data Layer

## Redis Access (db_msg_dispatcher)

The data layer currently supports only **Redis** (`src/server_frame/dispatcher/db_msg_dispatcher.{h,cpp}`):

- Based on hiredis-happ, it manages both **cluster** and **raw (sentinel)** connections;
- Supports `SCRIPT LOAD` and embedded Lua (CAS verification, KL index trimming);
- After a reply is unpacked, the waiting coroutine is resumed by task id.

## Logical Primitives (rpc/db/hash_table)

`src/server_frame/rpc/db/hash_table.{h,cpp}` provides business-friendly primitives on top of Redis:

| Primitive | Semantics |
| --- | --- |
| KV | `get_all` / `partly_get` / `batch` reads; `set` (optionally with CAS version); `inc_field` atomic increment |
| KL | Key-list index: `add` / `get` / `update` / `remove`, monotonic index list trimming (embedded Lua) |
| TTL | Key expiration management |

## Generated DB Interfaces

Generated from the table/index extensions in `svr.local.table.proto` / `svr.global.table.proto` +
`svr.database.extension.proto`, via `db_interface.*.mako` and `db_rpc_redis(.kv/.kl).*.mako`:

- `rpc/db/local_db_interface.atfw.gen.{h,cpp}`: interface for the local-zone database;
- `rpc/db/global_db_interface.atfw.gen.{h,cpp}`: interface for the global database;
- Namespaces look like `rpc::db::login_auth` / `rpc::db::dtmq_channel_record`, etc.; each table provides
  coroutine-based APIs such as get/set/replace/CAS.

```cpp
// Example: reading a table inside a coroutine
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get(ctx, user_id, ...));
```

## UUID

`src/server_frame/rpc/db/uuid.{h,cpp}` provides ID generation: `standard` / `short` / `global_increase`
(DB auto-increment) / `global_unique`.

## Session and Player Cache

- `src/server_frame/data/session.{h,cpp}`: gateway session, key = `(gateway_node_id, session_id)`, responsible
  for downstream sending;
- `src/server_frame/data/player_cache.{h,cpp}`: player data cache base class with dirty marking and init-task
  waiting;
- `src/server_frame/logic/session_manager.*` / `player_manager.*`: the corresponding manager singletons.
