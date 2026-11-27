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
| KV | `get_all` / `partly_get` / `batch_get_all` / `batch_partly_get`; `set` / `insert` / `partly_set`; atomic `inc_field` |
| KL | `add_index` / `get_all` / `get_by_indexs` / `update_by_index` / `remove_by_index`; monotonically increasing indexes with count-based trimming (embedded Lua) |
| KM | `get_all` / `get_by_map_keys`; `replace` / `remove_by_map_keys` (`replace` is the only write path); several records per Redis key, split by map key |
| TTL | Key expiration management |

KV `set` / `insert` write paths serialize and overwrite every field; unset fields become empty/default values rather
than being presence-merged. `partly_set` writes only the fields the caller passes
(`gsl::span<const google::protobuf::FieldDescriptor*>`) and leaves every other stored field untouched; it supports both
the plain `HSET` and the CAS `EVALSHA` path. Field values are still written as defaults when unset, so it is not a
presence merge either.

Business code does not build the field list by hand; it uses the fixed interface generated from the index
`partly_save` option:

```protobuf
index: {
  name: "user"
  key_fields: "zone_id"
  key_fields: "user_id"
  kv: {
    enable_cas: true
    partly_save: { name: "async_jobs" fields: "async_job_blob_data" }
  }
}
```

This generates `rpc::db::user::partly_save_async_jobs(ctx, zone_id, user_id, store, version)`, which writes the key
fields plus the declared fields; the descriptor array is a fixed stack array in the generated code. `partly_save` only
exists in the `kv` index options, so declaring it under `kl` / `km` does not compile.

### Map Indexes (km)

`km` keeps several sub-records of one Redis key inside a single hash: the key follows the same
`"{prefix}-{index name}:{key fields}"` layout, every hash field name is the joined text of the index `map_keys` values
(separated by `:`), and the field value is the whole record serialized as protobuf (`&` prefix, same payload format as
KL records). The primary keys and map keys are stored too, so a read returns the complete record.

```protobuf
index: {
  name: "user_item"
  key_fields: "zone_id"
  key_fields: "user_id"
  km: {
    enable_cas: true
    map_keys: "bag_type"
    map_keys: "slot"
  }
}
```

Generated `rpc::db::user_item::*`. The `map_keys` argument is a strongly typed struct generated per index, whose
fields are exactly the declared `map_keys` fields (the text encoding stays internal to the generated code):

```cpp
struct table_user_item_map_key {
  uint32_t bag_type{};
  uint32_t slot{};

  table_user_item_map_key() = default;
  // Copies every map_keys field from the record, so callers do not repeat the field list
  explicit table_user_item_map_key(const PROJECT_NAMESPACE_ID::table_user_item &store)
      : bag_type(store.bag_type()), slot(store.slot()) {}
};
```

The default constructor is kept (fields zero-initialized); use `table_user_item_map_key{*store}` to build a map key
straight from a record.

| Interface | Semantics |
| --- | --- |
| `get_all(ctx, zone_id, user_id, output[, version])` | `HGETALL`; records come back in backend order (not guaranteed to be sorted by map key), `output` is a `std::vector<atfw::util::memory::strong_rc_ptr<rpc::shared_message<table>>>` of complete records |
| `get_by_map_key(ctx, key..., gsl::span<const <table>_map_key>, output[, version])` | `HMGET`; missing (NIL) map keys are absent from `output`, all-missing still succeeds; CAS indexes also request the version field and report the record's CAS version |
| `replace_by_map_key(ctx, stores[, version])` | Batch upsert (both insert and update); every record must belong to the same key, otherwise `EN_SYS_PARAM` |
| `remove_by_map_key(ctx, key..., gsl::span<const <table>_map_key>)` | Removes the fields with a plain `HDEL`; neither checks nor advances the CAS version |

The CAS version lives in the same hash (`CAS_VERSION`) and is shared by every map record under one primary key — map
keys have no version of their own. `replace` reuses `kCompareAndSetHashTable` and no Lua script is added. The remove is
a plain `HDEL` that neither checks nor advances the version, so the key survives (holding only the version field) once
the last data field is deleted. Non-CAS indexes map to plain `HSET` / `HDEL` and never write a version field.

`map_keys` must not be empty and may only contain integer (including enum) and `bool` fields: the field name is their
text form, which is not unambiguous for floating point and strings, so the template emits `#error`.

### Generated DB Interfaces

Generated from the table/index extensions in `svr.local.table.proto` / `svr.global.table.proto` +
`svr.database.extension.proto`, via `db_interface.*.mako` and `db_rpc_redis(.kv/.kl/.km).*.mako`:

- `rpc/db/local_db_interface.atfw.gen.{h,cpp}`: interface for the local-zone database;
- `rpc/db/global_db_interface.atfw.gen.{h,cpp}`: interface for the global database;
- Namespaces look like `rpc::db::login_auth` / `rpc::db::dtmq_channel_record`, etc.; index options determine generated
  read, `insert`, `replace`, and other coroutine APIs. Only CAS-enabled indexes have version parameters.

```cpp
// Example: reading a table inside a coroutine
PROJECT_NAMESPACE_ID::table_login_auth row;
uint64_t version = 0;
int32_t result = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, account_type, open_id, row, version));
```

Current generation templates join Redis key fields with `:`, replacing the old `.` separator. `login_auth` also
changed to the `(account_type, open_id)` composite key. Reads have no old-key fallback, and the repository has no
automatic migration script. Upgrades must migrate keys for every affected table plus the login composite key and
coordinate reader/writer versions; mixed old/new versions must not be assumed to see the same records.

### UUID

`src/server_frame/rpc/db/uuid.{h,cpp}` provides ID generation: `standard` / `short` / `global_increase`
(DB auto-increment) / `global_unique`.

### Session and User Cache

- `src/component/user/sdk/data/session.{h,cpp}`: gateway session, key = `(gateway_node_id, session_id)`, responsible
  for downstream sending;
- `src/component/user/sdk/data/user_cache.{h,cpp}`: user data cache base class with dirty marking and init-task
  waiting;
- `src/component/user/sdk/logic/session_manager.*` / `user_manager.*`: the corresponding manager singletons.
