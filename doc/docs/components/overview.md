---
title: 组件总览
---

# 公共组件总览

`src/component/` 下的组件都是"服务 + 客户端 SDK + 协议"三件套形态，业务服务链接对应 SDK 即可使用。

| 组件 | 服务 | SDK | 说明 |
| --- | --- | --- | --- |
| [dtmq](dtmq) | `dtmq-proxysvr` | `dtmq-proxy-sdk` / `dtmq-common-sdk` | 分布式消息队列：频道订阅、消息收发、WAL 主从复制 |
| [distributed_transaction](distributed-transaction) | `dtcoordsvr` | `sdk/` | 分布式事务（2PC）：协调者 + 参与者/发起者句柄 |
| [rank](rank) | `rank_board_svr` | `sdk/` | 排行榜：分片榜单、镜像、WAL 主备同步 |
| [orbit](orbit) | controller / agent | 四套 SDK | Orbit 调度框架（controller/agent/server/client） |

另有：

- `GameSharedComponent/`：游戏共享纯算法库（`BattleUtility` 随机数、`ItemAlgorithm` 背包格子算法、
  `Orbit` 客户端运行时），无独立进程；
- `component/test/`：组件单元测试（`ItemAlgorithmTest` 等）。

## 组件的通用结构

```
component/<name>/
├── protocol/           # 组件协议（pbdesc + config）
├── <name>-*svr/        # 服务实现（task action + 数据层）
├── sdk/                # 客户端 SDK（生成的调用端 stub + 封装 API）
└── CMakeLists.txt      # 协议目标与 RPC 生成规则
```

服务的 RPC handler 与 task action 骨架由 Mako 模板生成；SDK 内含生成的调用端代码
（`*svrservice.atfw.gen.*`）与手工封装的易用 API。

## 当前业务接入情况

- **lobbysvr** 已接入 dtmq（`logic/dtmq/task_action_channel_event_sync.*` 接收频道事件流，典型用于聊天
  频道下发）与 rank（`rank/` 排行榜查询）；
- **rank_settlement_svr** 配合 rank 组件做周期结算；
- **orbitsvr** 演示 orbit 组件的 server/client RPC。
