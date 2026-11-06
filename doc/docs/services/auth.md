---
title: 登录鉴权与账号管理
---

# 登录鉴权与账号管理

`authsvr` 通过 `AuthsvrClientService` 处理登录鉴权。登录记录以 `(account_type, open_id)` 为复合键；
渠道标识属于账号键的一部分，不能用同一 `open_id` 复用其他渠道的记录。

## 内部账号

`internal_auth.enable` 开启内部账号，`internal_auth.allow_register` 控制是否接受新账号。已有账号的
`last_account_data.access` 与请求中的 `access` 直接按字节比较；当前实现不提供密码哈希，适用于受控的
开发和测试环境，不应被描述为生产密码安全方案。

预生成账号使用 GM 命令：

```text
auth-gen-accounts <count> <open-id prefix> [start index]
```

`count` 范围为 1 到 10000，`open-id prefix` 必填，`start index` 默认为 1 且必须为正数。账号类型固定为
internal。已有账号不会被覆盖；预生成账号可在 `allow_register=false` 时登录。

GM 回调只报告异步任务是否成功调度，不代表全部数据库写入已经完成。生成的 `open_id` 和密码明文写入服务
日志，必须按敏感信息限制日志访问、保留和转发。

## Steam 离线验票

启用 Steam 需要构建时包含 Steamworks SDK，并设置 `steam_auth.enable=true`。当前只实现 Encrypted App
Ticket 离线验票：

- 使用 `rgub_key`，或其为空时使用十六进制 `rgub_key_hex` 解密票据；
- 校验 `app_id`、票据签发时间和 SteamID 与 `open_id` 的一致性；
- `offline_token_timeout` 限制离线票据有效期，默认 504 小时。

`enable_offline=false` 不会切换到在线验证，只会让 Steam 校验失败。`online_checking_interval` 虽已声明，
当前没有在线验证实现或生效的周期检查；在线路径仍是 TODO。

## 数据升级

当前登录读取使用 `(account_type, open_id)` 复合 key，没有旧 key 回退。升级旧数据前必须规划 Redis key
和复合键迁移，并协调 Authsvr 与消费方版本；详见[数据层](../architecture/data-layer)。

实现入口为 `src/authsvr/service/logic/algorithm/auth_validate.cpp`、
`src/authsvr/service/logic/algorithm/auth_account_generator.cpp` 和
`src/authsvr/service/app/authsvr_main.cpp`。
