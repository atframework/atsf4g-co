---
title: Adding a Database Table
---

# Adding a Database Table

The data layer only supports Redis; table structures are described with protos and access code is generated
from templates.

## Steps

1. **Define the table proto**: in `src/server_frame/protocol/private/protocol/pbdesc/`, add a message to
   `svr.local.table.proto` (this zone's database) or `svr.global.table.proto` (the global database), and
   annotate primary keys, indexes (KV/KL), TTL, CAS fields, etc. using the extensions in
   `svr.database.extension.proto`.
2. **Rebuild**: `db_interface.*.mako` + `db_rpc_redis(.kv/.kl).*.mako` generate
   `rpc/db/local_db_interface.atfw.gen.{h,cpp}` or `global_db_interface.atfw.gen.{h,cpp}`,
   under the namespace `rpc::db::<table_name>`.
3. **Use it**: await directly inside a coroutine:

```cpp
// Read an entire row (version is the output CAS version)
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::my_table::get_all(ctx, key, msg, version));
// Replace the whole row (pass the read CAS version for optimistic locking; the new version is written back)
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::my_table::replace(ctx, std::move(store), version));
// Atomic increment of a numeric field (one generated API per incrementable field; inc_value takes the
// delta and returns the new value)
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::my_table::inc_field_<field_name>(ctx, key, inc_value));
```

Generated table APIs also include `batch_get_all`, `remove_all`, `set_ttl` / `remove_ttl`, and more.

## Low-Level Primitives

If the generated interface does not meet your needs, you can directly assemble the KV/KL/CAS/TTL primitives
in `rpc/db/hash_table.h` (refer to the implementation of `rpc/db/local_db_interface.atfw.gen.cpp`).

## ID Generation

When you need distributed IDs, use `rpc/db/uuid.h`: `standard` / `short` / `global_increase` (backed by DB
auto-increment) / `global_unique`.

## Configuration

Redis connections live in the `logic.db` section of the instance configuration (cluster plus sentinel/raw
dual channels), rendered on the deployment side by `install/**/cfg/*.yaml.tpl`.
