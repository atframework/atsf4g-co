---
title: Data Layer
---

# Data Layer

## Quick Start

1. Prepare Redis and configure service connections in values' `modules/redis.yaml`.
2. Use generated `rpc::db` APIs for existing tables; follow [database setup](../development/add-db-table) for new tables.
3. Await results with framework macros and check error codes. Ordinary business code needs no handwritten Redis commands or Lua.

## Customization and Design

### Redis Access (db_msg_dispatcher)

The data layer currently supports only **Redis** (`src/server_frame/dispatcher/db_msg_dispatcher.{h,cpp}`):

- Based on hiredis-happ, it manages both **cluster** and **raw (sentinel)** connections;
- Supports `SCRIPT LOAD` and embedded Lua (CAS verification, KL index trimming);
- After a reply is unpacked, the waiting coroutine is resumed by task id.

### Logical Primitives (rpc/db/hash_table)

`src/server_frame/rpc/db/hash_table.{h,cpp}` provides business-friendly primitives on top of Redis:

| Primitive | Semantics |
| --- | --- |
| KV | `get_all` / `partly_get` / `batch_get_all` / `batch_partly_get`; `set` / `insert`; atomic `inc_field` |
| KL | `add_index` / `get_all` / `get_by_indexs` / `update_by_index` / `remove_by_index`; monotonically increasing indexes with count-based trimming (embedded Lua) |
| TTL | Key expiration management |

### Generated DB Interfaces

Generated from the table/index extensions in `svr.local.table.proto` / `svr.global.table.proto` +
`svr.database.extension.proto`, via `db_interface.*.mako` and `db_rpc_redis(.kv/.kl).*.mako`:

- `rpc/db/local_db_interface.atfw.gen.{h,cpp}`: interface for the local-zone database;
- `rpc/db/global_db_interface.atfw.gen.{h,cpp}`: interface for the global database;
- Namespaces look like `rpc::db::login_auth` / `rpc::db::dtmq_channel_record`, etc.; index options determine generated
  read, `insert`, `replace`, and other coroutine APIs. Only CAS-enabled indexes have version parameters.

```cpp
// Example: reading a table inside a coroutine
PROJECT_NAMESPACE_ID::table_login_auth row;
uint64_t version = 0;
int32_t result = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, open_id, row, version));
```

### UUID

`src/server_frame/rpc/db/uuid.{h,cpp}` provides ID generation: `standard` / `short` / `global_increase`
(DB auto-increment) / `global_unique`.

### Session and User Cache

- `src/server_frame/data/session.{h,cpp}`: gateway session, key = `(gateway_node_id, session_id)`, responsible
  for downstream sending;
- `src/server_frame/data/user_cache.{h,cpp}`: user data cache base class with dirty marking and init-task
  waiting;
- `src/server_frame/logic/session_manager.*` / `user_manager.*`: the corresponding manager singletons.
