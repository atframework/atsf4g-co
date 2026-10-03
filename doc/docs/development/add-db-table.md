---
title: 新增数据库表
---

# 新增数据库表

内置数据层使用 Redis。表结构和访问选项由 proto 声明，工程自动生成接口。

## 快速上手

1. 在 `src/server_frame/protocol/private/protocol/pbdesc/svr.local.table.proto`（本 zone 库）
   或 `svr.global.table.proto`（全局库）中增加表。下面的声明追加到已有文件，复用其 import 和 package：

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

2. 按[统一构建步骤](overview)重新配置/构建；已有流程更新
   `rpc/db/local_db_interface.atfw.gen.h/.cpp` 或 `global_db_interface.atfw.gen.h/.cpp`。
   接口命名空间按索引的 `name` 生成，例如 `rpc::db::example`，无需修改模板。
3. 包含对应生成头文件，在业务 action 中按生成声明读写，并检查错误码。
   现有登录表的读取示例为：

```cpp
PROJECT_NAMESPACE_ID::table_login_auth row;
uint64_t version = 0;
int32_t result = RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::get_all(ctx, open_id, row, version));
```

   `ctx` 是当前 RPC context，`open_id` 是登录表的字符串键。新表的 key 和消息类型以生成接口为准。
   `replace` 使用 `rpc::shared_message<Table>`，不要直接传入普通 protobuf message。
4. 在 values 中配置 Redis 并生成实例 YAML；用有效 key 验证插入、读取和更新。
   启用 CAS 的表还应验证版本不匹配时更新失败。

## 常用接口

生成接口按存储选项提供 `get_all`、`batch_get_all`、`insert`、`replace`、`remove_all`、
TTL 与字段自增等。CAS 的 `version` 在读后保存并传给更新；具体参数与可用接口查生成头文件。
已有表增加字段通常只需修改 proto 和业务读写逻辑。

## 定制与详细设计

生成接口不能满足需求时，再使用 `rpc/db/hash_table.h` 的 KV/KL/CAS/TTL 原语。
表注解定义在 `private/protocol/extension/svr.database.extension.proto`，
生成规则见[RPC 与代码生成](../architecture/rpc-codegen)。
分布式 ID 接口在 `rpc/db/uuid.h`，数据层运行方式见[数据层](../architecture/data-layer)。
