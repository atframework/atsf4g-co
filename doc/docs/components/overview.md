---
title: 组件总览
---

# 公共组件与可复用服务

工程既提供独立组件，也提供可复用业务服务。日常接入先准备对应服务与配置，
再在消费方填写 SDK 依赖声明并使用公开 API，参见[组件接入](../development/add-component)。

## 按功能选择

| 功能 | 服务/位置 | 消费方 SDK 声明 |
| --- | --- | --- |
| [消息队列](dtmq) | `src/component/dtmq/`，`dtmq-proxysvr` | `USE_COMPONENTS "dtmq-proxy-sdk"` |
| [分布式事务](distributed-transaction) | `src/component/distributed_transaction/`，`dtcoordsvr` | `USE_COMPONENTS "distributed-transaction-sdk"` |
| [排行榜](rank) | `src/component/rank/`，`rank_board_svr` | `USE_COMPONENTS "rank-board-svr-sdk" "rank-logic-sdk"` |
| [好友](friend) | `src/friendsvr/`，管理与推荐服务 | `USE_SERVICE_SDK "friend-sdk-management"` |
| [匹配与组队](matching-team) | `src/matchsvr/`、`src/teamsvr/` | `USE_SERVICE_SDK "matchsvr-sdk" "team-common-sdk" "team-sdk-room"` |
| [UE DS 管理（Orbit）](orbit) | `src/component/orbit/`，controller / agent / server / client | 按进程角色选择对应 SDK，见 Orbit 上手 |
| 共享算法 | `src/component/GameSharedComponent/` | `USE_COMPONENTS "ItemAlgorithmSDK" "BattleUtilitySDK"` |

这些参数追加到已有服务声明，所需 API 决定具体依赖；无需为每个功能重建服务目标。
共享算法 SDK 不需要独立进程。Orbit 的 client 运行时也在 `GameSharedComponent/Orbit/`。

## 快速上手

1. 服务组件按[运行与部署](../getting-started/run-deploy)启动，按需准备 Redis、Excel 和实例布局。
   纯算法 SDK 直接从依赖声明开始。
2. 参照 `src/lobbysvr/service/CMakeLists.txt` 添加所需 SDK 参数，重新配置/构建。
3. 在业务 action 中调用 SDK；需要通知的组件同时注册通知 handler。
4. 服务组件验证请求、响应和所需的通知，纯算法验证调用结果。已有 `lobbysvr` 演示 DTMQ、排行榜、好友、匹配与组队接入，
   `orbitsvr` 演示 Orbit server 接入。

## 定制与详细设计

常见组件采用 `protocol/`、服务实现和 `sdk/` 的结构；纯算法组件只提供库。
协议、生成和链接声明参照[新增组件](../development/add-component)。
各组件后面的设计章节分别介绍持久化、副本、恢复与生命周期规则。
