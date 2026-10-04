---
title: 服务总览
---

# 服务总览

## 快速上手

先按[运行与部署](../getting-started/run-deploy)生成本地配置，再根据需要在 `proc_desc` 启用服务。
消费方通过 SDK 使用其他服务的能力；公共功能的接入见[组件总览](../components/overview)。

| 服务/部署 chart | 用途 | 最小验证入口 |
| --- | --- | --- |
| `echosvr` | 原始消息回显示例 | 经 atgateway 发送数据并检查回显 |
| `authsvr` | 登录鉴权，`AuthsvrClientService` | robot 登录请求与认证响应 |
| `cachesvr` | 分布式对象缓存，带 SDK | 参照大厅服 `cache/` 的现有缓存调用 |
| `lobbysvr` | 玩家登录、用户数据、好友、队伍、匹配及通知 | robot 登录后执行已有业务命令 |
| `friendsvr-management` / `friendsvr-recommend` | 好友管理；推荐策略待实现 | [好友上手](../components/friend) |
| `matchsvr` | 匹配池与房间 | [匹配上手](../components/matching-team) |
| `teamsvr-room` / `teamsvr-match` | 队伍房间；队友搜索服务待实现 | [组队上手](../components/matching-team) |
| `dtmq-proxysvr` | 消息频道、订阅与同步 | [DTMQ 上手](../components/dtmq) |
| `dtcoordsvr` | 分布式事务协调者 | [事务上手](../components/distributed-transaction) |
| `rank-board-svr` / `rank-settlement-svr` | 排行榜与周期结算 | [排行榜上手](../components/rank) |
| `orbit-controller` / `orbit-agent` / `orbit-server` | UE DS 管理；server 由 `orbitsvr` 示例实现 | [Orbit 上手](../components/orbit) |
| `atgateway` / `atproxy` | 客户端接入与跨服通信 | [接入配置](../architecture/gateway-proxy) |

部署 chart 的连字符名称与部分源码目录中的下划线名称不同，登记实例时使用实际 chart 名。

### 认证与大厅

启用 `authsvr`、`lobbysvr` 和所需的网关、代理、Redis/组件实例，沿用已有配置生成流程。
用 robot 完成登录鉴权，再调用大厅已有的用户信息或业务命令，检查响应与客户端通知。
新增登录策略或大厅业务方法从 [RPC 上手](../development/add-rpc-task)开始。

### 对象缓存

启用 `cachesvr` 并配置 Redis。消费方在已有声明的 `USE_SERVICE_SDK` 中加入 `cachesvr-sdk`，
参照 `src/lobbysvr/service/logic/cache/user_cache_manager.h/.cpp` 使用缓存 API。
先验证同一对象的读取和更新，需要订阅时再接入更新通知。

### 回显示例

将测试网关的目标配置为 `echosvr`，发送测试数据并检查原样回显。
它适合验证接入链路；需要用户/session 校验和业务 RPC 的新服务使用标准服务模板。

## 新增服务

参见[新增服务](../development/add-service)。标准服务保留公共装配和 handler 注册，
在业务 action 中实现逻辑。扩展已有服务的方法则直接按[RPC 上手](../development/add-rpc-task)操作。

## 定制与目录约定

```text
<name>svr/
├── protocol/       # 可选：服务协议与配置
├── service/        # app/ 入口、logic/ 业务及 task action
├── sdk/            # 可选：对外 API
└── CMakeLists.txt  # 使用已有 helper 的声明
```

各服务可含多个进程角色，实际结构以源码为准。
需要改变装配时参照 `src/server_frame/logic/logic_server_setup.h`；
压测和模拟客户端见[robot](robot)。
