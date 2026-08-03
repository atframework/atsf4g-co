---
slug: /
title: 项目简介
---

# atsf4g-co 文档

**atsf4g-co**（AT Service Framework for Game - Coroutine）是一套基于协程的游戏服务器框架，构建在
[atframework](https://github.com/atframework) 系列组件之上（`atframe_utils`、`libatbus`、`libatapp`、
`libcopp`），使用 C++17（在可用时使用 C++20/C++23 特性），面向可扩展、高性能的游戏服务器架构。

## 核心特性

- **协程化异步模型**：单进程单线程事件循环（libuv）+ 协程任务（libcopp cotask 或 C++20 协程），业务并发完全由
  task action 承载，告别回调地狱。
- **声明式 RPC 与代码生成**：以 protobuf 为协议源头，通过自定义扩展选项 + Mako 模板批量生成 RPC 调用端、
  分发 handler、task action 骨架、数据库访问层与配置加载代码。
- **内置基础设施**：atgateway（客户端网关，ECDH 握手/加密/限流）、atproxy（跨服代理，etcd 服务发现）、
  路由对象缓存（router）、Redis 数据层、OpenTelemetry 可观测性。
- **可复用组件**：分布式消息队列（dtmq）、分布式事务（2PC）、排行榜（rank board）、Orbit 等开箱即用的
  服务组件 + 客户端 SDK。
- **云原生部署**：Helm chart / Docker / 裸机脚本三种部署形态，由 atdtool 渲染 `install/**/*.tpl` 模板生成。

## 文档导航

| 章节 | 内容 |
| --- | --- |
| [快速开始](getting-started/prerequisites) | 环境准备、构建、运行与部署 |
| [架构设计](architecture/overview) | 组件关系、消息流、dispatcher/RPC/router/数据层/配置/可观测性 |
| [公共组件](components/overview) | dtmq、分布式事务、排行榜、Orbit |
| [服务](services/overview) | 各业务服务与压测机器人 |
| [开发指南](development/add-service) | 新增服务、新增 RPC、数据库表、Excel 配置、单元测试 |

## 仓库结构速览

```
atsf4g-co/
├── atframework/        # vendored 框架库（atframe_utils / libatbus / libatapp / atproxy / atgateway）
├── src/
│   ├── server_frame/   # 服务器公共框架：config / dispatcher / router / rpc / data / logic / utility
│   ├── *svr/           # 业务服务：echosvr、authsvr、cachesvr、lobbysvr、rank_settlement_svr、orbitsvr
│   ├── component/      # 可复用组件：dtmq、distributed_transaction、rank、orbit、GameSharedComponent
│   ├── templates/      # Mako 代码生成模板
│   ├── tools/          # generate-for-pb 代码生成器、etcd 调试工具
│   └── robot/          # Go 压测机器人
├── install/            # 部署模板（Helm chart + Go text/template）
├── resource/           # Excel 策划配置表与 xresloader 配置
├── project/            # CMake 构建选项与工具
└── third_party/        # 第三方依赖（预编译包、atdtool、xresloader 等）
```
