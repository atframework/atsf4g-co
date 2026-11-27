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
| KV | `get_all` / `partly_get` / `batch_get_all` / `batch_partly_get`；`set` / `insert` / `partly_set`；`inc_field` 原子自增 |
| KL | `add_index` / `get_all` / `get_by_indexs` / `update_by_index` / `remove_by_index`；索引单调递增，按数量裁剪（内嵌 Lua） |
| KM | `get_all` / `get_by_map_keys`；`replace` / `remove_by_map_keys`（`replace` 是唯一写路径）；一条 Redis 记录内按 map key 分多个字段 |
| TTL | 键过期管理 |

KV `set` / `insert` 写路径序列化并覆盖全部字段；未设置字段写成空值或默认值，不是 presence merge。
`partly_set` 只写调用方给出的字段（`gsl::span<const google::protobuf::FieldDescriptor*>`），其余字段保持库中原值，
同样支持非 CAS 的 `HSET` 与 CAS 的 `EVALSHA` 两种写路径；字段值为空时按默认值写入，不做 presence merge。

业务侧不直接拼字段列表，而是通过索引上的 `partly_save` 声明生成固定接口：

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

生成 `rpc::db::user::partly_save_async_jobs(ctx, zone_id, user_id, store, version)`：写回 key 字段与声明的字段，
字段描述符数组在生成代码里固定为栈数组，不动态分配。`partly_save` 只定义在 `kv` 索引选项里，声明在 `kl` / `km`
下无法通过 proto 编译。

### Map 索引（km）

`km` 把同一条 Redis 记录的多个子记录放在一个 hash 里：key 和其他类型一样是 `"{前缀}-{索引名}:{key 字段}"`，
hash 的 field 名是索引 `map_keys` 各字段值用 `:` 拼接的文本，field 值是该记录整条 protobuf 的序列化结果
（`&` 前缀，与 KL 的记录值格式一致；主 key 与 map key 字段也一起存，读回即为完整记录）。

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

生成 `rpc::db::user_item::*`。`map_keys` 入参是索引专属的强类型 struct，字段名与类型就是声明的 `map_keys`
字段（文本编码只在生成代码内部使用，不对外暴露）：

```cpp
struct table_user_item_map_key {
  uint32_t bag_type{};
  uint32_t slot{};

  table_user_item_map_key() = default;
  // 直接从记录取所有 map_keys 字段，调用方不必逐字段赋值
  explicit table_user_item_map_key(const PROJECT_NAMESPACE_ID::table_user_item &store)
      : bag_type(store.bag_type()), slot(store.slot()) {}
};
```

默认构造保留（字段零初始化）；需要从一条记录构造 map key 时用 `table_user_item_map_key{*store}`。

| 接口 | 语义 |
| --- | --- |
| `get_all(ctx, zone_id, user_id, output[, version])` | `HGETALL` 全量读取，记录顺序由后端决定（不保证按 map key 排序），`output` 是 `std::vector<atfw::util::memory::strong_rc_ptr<rpc::shared_message<表>>>`，每条记录即完整 protobuf |
| `get_by_map_key(ctx, key..., gsl::span<const <表>_map_key>, output[, version])` | `HMGET`；不存在（NIL）的 map key 不出现在 `output`，全部不存在也算成功；CAS 索引把版本字段一并请求，返回整条记录的 CAS 版本 |
| `replace_by_map_key(ctx, stores[, version])` | 批量 upsert（新增与更新都走它）；所有记录必须同属一个 key，否则返回 `EN_SYS_PARAM` |
| `remove_by_map_key(ctx, key..., gsl::span<const <表>_map_key>)` | 删除指定 field；纯 `HDEL`，不校验也不推进 CAS 版本 |

CAS 版本存同一个 hash 的 `CAS_VERSION` field，同一个主 key 下的所有 map 记录共用一个版本，map key 本身没有独立版本：
`replace` 复用 `kCompareAndSetHashTable`，不新增 Lua 脚本。删除是纯 `HDEL`，既不校验也不推进版本，因此删掉最后一个
数据 field 后 key 仍然存在（只剩版本字段），记录集为空。非 CAS 索引对应 `HSET` / `HDEL`，不写版本字段。

`map_keys` 不能为空，且只允许整数（含枚举）与 `bool` 字段：field 名是这些值的文本拼接，浮点与字符串的文本形式
不唯一，模板会生成 `#error`。

### 生成的 DB 接口

由 `svr.local.table.proto` / `svr.global.table.proto` + `svr.database.extension.proto` 的表/索引扩展，经
`db_interface.*.mako` 与 `db_rpc_redis(.kv/.kl/.km).*.mako` 生成：

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
