---
title: Adding a Database Table
---

# Adding a Database Table

The built-in data layer uses Redis. Proto declarations define tables/access options and drive generated APIs.

## Quick Start

1. Add a table to `src/server_frame/protocol/private/protocol/pbdesc/svr.local.table.proto`
   (zone-local DB) or `svr.global.table.proto` (global DB).
   Append this declaration to the existing file, reusing its imports/package:

```protobuf
message table_example {
  option (atframework.database_table) = {
    index: {
      name: "example"
      type: EN_ATFRAMEWORK_DB_INDEX_TYPE_KV
      enable_cas: true
      key_fields: "user_id"
    }
  };
  uint64 user_id = 1;
  string value = 2;
}
```

2. [Reconfigure/build](overview); the existing workflow updates
   `rpc/db/local_db_interface.atfw.gen.h/.cpp` or `global_db_interface.atfw.gen.h/.cpp`.
   API namespaces follow the index `name`, for example `rpc::db::example`. No template changes are needed.
3. Include the generated header, use its declarations from business actions, and check error codes.
   The existing login table can be read as follows:

```cpp
PROJECT_NAMESPACE_ID::table_login_auth row;
uint64_t version = 0;
int32_t result = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, open_id, row, version));
```

   `ctx` is the current RPC context and `open_id` is the login table's string key.
   New table keys/message types follow their generated APIs. `replace` takes
   `rpc::shared_message<Table>`, rather than a plain protobuf message.
4. Configure Redis in values and generate instance YAML. Verify insert, read, and update with valid keys.
   For CAS-enabled tables, also verify that a version mismatch rejects the update.

## Common APIs

Storage options determine generated `get_all`, `batch_get_all`, `insert`, `replace`, `remove_all`,
TTL, and field-increment APIs. Keep the CAS `version` returned by reads and pass it to updates;
check generated headers for actual signatures and available APIs.
Adding fields to an existing table usually needs only proto and business read/write changes.

## Customization and Design

Use `rpc/db/hash_table.h` KV/KL/CAS/TTL primitives when generated APIs do not meet your needs.
Annotations are defined in `private/protocol/extension/svr.database.extension.proto`;
see [RPC and code generation](../architecture/rpc-codegen) for generation rules.
Distributed IDs use `rpc/db/uuid.h`; see [data layer](../architecture/data-layer) for runtime behavior.
