---
title: 数据层
---

# 数据层

## 快速上手

1. 准备 Redis，并在 values 的 `modules/redis.yaml` 填写服务连接配置。
2. 在已有表使用生成的 `rpc::db` 接口；新增表按[数据库上手](../development/add-db-table)声明。
3. 在 action 中用框架协程宏等待结果并检查错误码；普通业务不需要手写 Redis 命令或 Lua。

## 定制与详细设计

### Redis 访问（db_msg_dispatcher）

数据层目前只支持 **Redis**（`src/server_frame/dispatcher/db_msg_dispatcher.{h,cpp}`）：

- 基于 hiredis-happ，同时管理 **cluster** 与 **raw（sentinel）** 两类连接；
- 支持 `SCRIPT LOAD` 与内嵌 Lua（CAS 校验、KL 索引裁剪）；
- 回复解包后按 task id 唤醒等待的协程。

### 逻辑原语（rpc/db/hash_table）

`src/server_frame/rpc/db/hash_table.{h,cpp}` 在 Redis 之上提供业务友好的原语：

| 原语 | 语义 |
| --- | --- |
| KV | `get_all` / `partly_get` / `batch_get_all` / `batch_partly_get`；`set` / `insert`；`inc_field` 原子自增 |
| KL | `add_index` / `get_all` / `get_by_indexs` / `update_by_index` / `remove_by_index`；索引单调递增，按数量裁剪（内嵌 Lua） |
| TTL | 键过期管理 |

当前 KV `set` / `insert` 写路径序列化并覆盖全部字段；未设置字段写成空值或默认值，不是 presence merge。
需要部分更新时使用对应的显式字段操作，并确认生成接口是否提供该语义。

### 生成的 DB 接口

由 `svr.local.table.proto` / `svr.global.table.proto` + `svr.database.extension.proto` 的表/索引扩展，经
`db_interface.*.mako` 与 `db_rpc_redis(.kv/.kl).*.mako` 生成：

- `rpc/db/local_db_interface.atfw.gen.{h,cpp}`：本 zone 库接口；
- `rpc/db/global_db_interface.atfw.gen.{h,cpp}`：全局库接口；
- 命名空间形如 `rpc::db::login_auth` / `rpc::db::dtmq_channel_record` 等，按各索引选项生成读取、
  `insert`、`replace` 等协程化 API；只有启用 CAS 的索引才有对应版本参数。

```cpp
// 示例：协程内读表
PROJECT_NAMESPACE_ID::table_login_auth row;
uint64_t version = 0;
int32_t result = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, account_type, open_id, row, version));
```

当前生成模板用 `:` 连接 Redis key 的各字段，不再使用旧的 `.` 分隔符。`login_auth` 同时从单字段 key 改为
`(account_type, open_id)` 复合 key。读取没有旧 key 回退，仓库也没有自动迁移脚本；升级时必须迁移全部受影响
表的 key 和登录复合键，并协调读写版本，不能假定并行运行的新旧版本看到同一记录。

### UUID

`src/server_frame/rpc/db/uuid.{h,cpp}` 提供 ID 生成：`standard` / `short` / `global_increase`（DB 自增）/
`global_unique`。

### 会话与用户缓存

- `src/component/user/sdk/data/session.{h,cpp}`：网关会话，key = `(gateway_node_id, session_id)`，负责下行发送；
- `src/component/user/sdk/data/user_cache.{h,cpp}`：用户数据缓存基类，脏标记、初始化 task 等待；
- `src/component/user/sdk/logic/session_manager.*` / `user_manager.*`：对应管理单例。
