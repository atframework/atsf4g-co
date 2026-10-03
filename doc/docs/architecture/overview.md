---
title: 架构总览
---

# 架构总览

## 组件关系

```mermaid
flowchart LR
    Client([游戏客户端]) <-->|"atgateway v2 协议<br/>ECDH 握手/加密/压缩"| GW[atgateway]
    GW <-->|atbus| Proxy[atproxy]
    Proxy <-->|atbus| SVC[业务服务<br/>lobbysvr / authsvr / cachesvr / ...]
    GW <-->|atbus| SVC
    SVC <--> Redis[(Redis<br/>cluster/sentinel)]
    Proxy <--> Etcd[(etcd<br/>服务发现/在线检测)]
    SVC <--> Etcd
    SVC --> OTel[(OpenTelemetry<br/>Collector)]

    subgraph 业务服务内部
      CS[cs_msg_dispatcher] --> TM[task_manager<br/>协程 task]
      SS[ss_msg_dispatcher] --> TM
      DBD[db_msg_dispatcher] --> TM
      TM --> RT[router_manager_set<br/>路由对象缓存]
    end
```

主链路：**Client → atgateway → atproxy → service → dispatcher → task action → data/DB**。

- **atgateway**（`atframework/service/atgateway`）：管理客户端连接，负责 ECDH/DH 密钥交换、加密、压缩、
  限流与路由切换。
- **atproxy**（`atframework/service/atproxy`）：跨服务通信代理，使用 etcd 做服务发现与在线检测。
- **业务服务**（`src/*svr/`、`src/component/*/`）：统一装配自 `src/server_frame/`，通过 dispatcher 将消息
  转为协程 task action 执行。

## 分层视图

| 层 | 位置 | 职责 |
| --- | --- | --- |
| 接入层 | atgateway / atproxy | 客户端接入、跨服转发、服务发现 |
| 框架层 | `src/server_frame/` | dispatcher、task、router、rpc、config、data、telemetry |
| 组件层 | `src/component/` | dtmq、distributed_transaction、rank、orbit 等可复用服务 + SDK |
| 业务层 | `src/*svr/` | 登录、大厅、缓存、好友、匹配、组队等可复用服务与业务逻辑 |
| 数据层 | Redis（`db_msg_dispatcher`） | KV/KL/CAS 原语 + 由 `*.table.proto` 生成的 DB 接口 |
| 部署层 | `install/` | Helm chart / Docker / 裸机脚本，atdtool 渲染 |

## vendored 框架库（atframework/）

| 库 | 职责 |
| --- | --- |
| `atframe_utils` | 基础工具：日志、算法、协程封装、分布式系统原语（WAL 等） |
| `libatbus` | 服务器间通信总线 |
| `libatapp` | 服务器应用框架：模块管理、事件循环、配置、连接器 |
| `service/atproxy` / `service/atgateway` | 内置接入服务 |
| `cmake-toolset` | 跨平台 CMake 工具链与第三方 ports |

## 单线程协程模型

服务业务任务通常在主 libuv 事件循环上调度，atbus、Redis 等异步请求通过协程等待结果。
这不表示所有依赖都没有后台线程；例如 Orbit client 运行时含独立线程与线程间队列。
业务逻辑应遵循所用模块的线程与生命周期约定。`PROJECT_SERVER_FRAME_USE_STD_COROUTINE` 开关在 C++20 协程与 libcopp cotask 两套实现间
切换，由 `task_type_traits.h` 统一抽象，业务代码无需感知差异。
