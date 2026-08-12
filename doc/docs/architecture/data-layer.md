---
title: 数据层
---

# 数据层

## Redis 访问（db_msg_dispatcher）

数据层目前只支持 **Redis**（`src/server_frame/dispatcher/db_msg_dispatcher.{h,cpp}`）：

- 基于 hiredis-happ，同时管理 **cluster** 与 **raw（sentinel）** 两类连接；
- 支持 `SCRIPT LOAD` 与内嵌 Lua（CAS 校验、KL 索引裁剪）；
- 回复解包后按 task id 唤醒等待的协程。

## 逻辑原语（rpc/db/hash_table）

`src/server_frame/rpc/db/hash_table.{h,cpp}` 在 Redis 之上提供业务友好的原语：

| 原语 | 语义 |
| --- | --- |
| KV | `get_all` / `partly_get` / `batch` 读取；`set`（可带 CAS 版本）；`inc_field` 原子自增 |
| KL | 键列表索引：`add` / `get` / `update` / `remove`，索引列表单调裁剪（内嵌 Lua） |
| TTL | 键过期管理 |

## 生成的 DB 接口

由 `svr.local.table.proto` / `svr.global.table.proto` + `svr.database.extension.proto` 的表/索引扩展，经
`db_interface.*.mako` 与 `db_rpc_redis(.kv/.kl).*.mako` 生成：

- `rpc/db/local_db_interface.atfw.gen.{h,cpp}`：本 zone 库接口；
- `rpc/db/global_db_interface.atfw.gen.{h,cpp}`：全局库接口；
- 命名空间形如 `rpc::db::login_auth` / `rpc::db::dtmq_channel_record` 等，每个表提供 get/set/replace/CAS 等
  协程化 API。

```cpp
// 示例：协程内读表
auto res = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get(ctx, user_id, ...));
```

## UUID

`src/server_frame/rpc/db/uuid.{h,cpp}` 提供 ID 生成：`standard` / `short` / `global_increase`（DB 自增）/
`global_unique`。

## 会话与用户缓存

- `src/server_frame/data/session.{h,cpp}`：网关会话，key = `(gateway_node_id, session_id)`，负责下行发送；
- `src/server_frame/data/user_cache.{h,cpp}`：用户数据缓存基类，脏标记、初始化 task 等待；
- `src/server_frame/logic/session_manager.*` / `user_manager.*`：对应管理单例。
