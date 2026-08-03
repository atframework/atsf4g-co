---
title: 新增数据库表
---

# 新增数据库表

数据层只支持 Redis；表结构用 proto 描述，由模板生成访问代码。

## 步骤

1. **定义表 proto**：在 `src/server_frame/protocol/private/protocol/pbdesc/` 的
   `svr.local.table.proto`（本 zone 库）或 `svr.global.table.proto`（全局库）中添加 message，并用
   `svr.database.extension.proto` 的扩展标注主键、索引（KV/KL）、TTL、CAS 字段等。
2. **重新构建**：`db_interface.*.mako` + `db_rpc_redis(.kv/.kl).*.mako` 生成
   `rpc/db/local_db_interface.atfw.gen.{h,cpp}` 或 `global_db_interface.atfw.gen.{h,cpp}`，
   命名空间为 `rpc::db::<table_name>`。
3. **使用**：协程内直接 await：

```cpp
// 读取整行（version 为输出的 CAS 版本）
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::my_table::get_all(ctx, key, msg, version));
// 整体替换（version 传入读到的 CAS 版本做乐观锁，成功后写回新版本）
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::my_table::replace(ctx, std::move(store), version));
// 数值字段原子自增（每个可自增字段生成独立接口，inc_value 为输入增量并写回新值）
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::my_table::inc_field_<field_name>(ctx, key, inc_value));
```

生成的表接口还包括 `batch_get_all`、`remove_all`、`set_ttl` / `remove_ttl` 等。

## 底层原语

如果生成的接口不满足需求，可直接使用 `rpc/db/hash_table.h` 的 KV/KL/CAS/TTL 原语自行组装（参考
`rpc/db/local_db_interface.atfw.gen.cpp` 的实现方式）。

## ID 生成

需要分布式 ID 时使用 `rpc/db/uuid.h`：`standard` / `short` / `global_increase`（走 DB 自增）/
`global_unique`。

## 配置

Redis 连接在实例配置的 `logic.db` 段（cluster 与 sentinel/raw 双通道），部署侧由
`install/**/cfg/*.yaml.tpl` 渲染。
