---
title: 好友服务
---

# 好友服务

`src/friendsvr/` 提供好友管理与推荐服务，配套协议和 SDK。
大厅服的 `logic/friend_api/` 接入好友列表、邀请、接受/拒绝和删除。
当前推荐服务的 `task_action_recommend_search` 与大厅 `friend_get_suggest` 仍未实现推荐策略，
调用成功不表示已有推荐结果。
大厅服保留了礼物接口与本地记录逻辑，但好友管理事务当前拒绝礼物事件，流程尚未贯通。
礼物道具配置、实际道具填充与发放也仍待业务接入。

## 快速上手

1. 在 values 的实例布局中启用 `friendsvr-management`，准备 Redis，
   沿用 `modules/friend_api.yaml` 和相应 chart 的配置。
   好友管理 SDK 依赖分布式事务，事务服务准备见[分布式事务](distributed-transaction)。
2. 在消费方服务的 `USE_SERVICE_SDK` 中加入 `friend-sdk-management`，重新配置/构建。
   推荐服务框架可用于定制业务策略，不属于本节最小接入。
3. 业务封装参照 `src/lobbysvr/service/logic/friend_api/user_friend_api_manager.h/.cpp`，
   数据访问使用 SDK 的 `data/friend_cache.h` 与 `router/router_friend_manager.h`。
4. 沿用大厅服对 `FriendManagementNotifyService` 的生成声明与 handler 注册，处理好友数据通知。
5. 用两个测试用户完成“发送邀请 → 接受 → 查询双方列表 → 删除”，核对双方数据与通知。
   客户端消息定义见 `com.protocol.friend_api.proto`，robot 的 `cmd/friend.go` 提供现有命令入口。

已有邀请与好友数量限制从配置入口调整；修改 RPC 声明按[RPC 上手](../development/add-rpc-task)。

## 定制与详细设计

好友管理协议在 `src/friendsvr/protocol/management/`，推荐协议在
`protocol/recommend/`。事务参与者与 WAL 实现位于
`service/management/data/`；变更双方关系持久化或恢复行为时需核对这些实现。
`src/friendsvr/test/friendsvr_management_test.cpp` 和大厅服好友测试是验证入口。
