---
slug: /
title: 项目简介
---

# atsf4g-co 文档

**atsf4g-co**（AT Service Framework for Game - Coroutine）是一套基于协程的游戏服务器框架，构建在
[atframework](https://github.com/atframework) 系列组件之上（`atframe_utils`、`libatbus`、`libatapp`、
`libcopp`）。项目代码要求 **C++14**，支持 **Windows、Linux 和 macOS**。

## 核心特性

- **协程化异步模型**：支持 C++20 协程和 libcopp 传统有栈协程。通过
  `PROJECT_SERVER_FRAME_USE_STD_COROUTINE` 一键切换后端，使用框架协程接口的业务代码无需修改。
  C++20 后端需要支持标准协程的工具链；C++14 使用有栈后端。
- **声明式开发**：协议以 protobuf 为源头，已有生成流程自动生成 RPC 调用接口、handler 注册、
  task action 骨架、数据库接口和 Excel 配置加载代码。日常扩展只需填写声明配置并实现业务逻辑。
- **跨平台基础设施**：atgateway 提供客户端接入，atproxy 提供跨服通信；内置服务发现、
  路由对象缓存、Redis 数据层和 OpenTelemetry 可观测性。
- **可复用组件与服务**：分布式消息队列（dtmq）、分布式事务、排行榜、好友服务、匹配服务、
  组队服务及其 SDK。
- **Orbit**：针对 Unreal Engine（UE）的 Dedicated Server（DS）管理解决方案，通过
  controller / agent / server / client 管理进程生命周期、心跳和通信。
- **部署工具**：使用 atdtool 和 values 配置生成 Kubernetes、Docker 或裸机/本地运行所需的资源与脚本。

项目代码的 C++14 要求与第三方依赖的工具链要求分别核对；选用较新依赖版本时，构建可能采用更高的语言标准。
参见[环境准备](getting-started/prerequisites)和[协程后端切换](getting-started/build#coroutine-backend)。

## 从哪里开始

首次使用按[环境准备](getting-started/prerequisites) → [构建](getting-started/build) →
[运行与部署](getting-started/run-deploy)操作。已有可运行工程时，直接进入[开发快速上手](development/overview)，
按要完成的任务选择章节。

| 章节 | 内容 |
| --- | --- |
| [开发快速上手](development/overview) | RPC、服务、组件、Excel、服务端配置、协议与数据库表的最小操作路径 |
| [公共组件](components/overview) | dtmq、分布式事务、排行榜、好友、匹配、组队与 Orbit 的接入入口 |
| [服务](services/overview) | 各服务职责与本地验证入口 |
| [架构设计](architecture/overview) | 需要修改底层行为时查阅的设计与实现说明 |

## 仓库结构速览

```text
atsf4g-co/
├── atframework/        # 框架库与 atproxy / atgateway
├── src/
│   ├── server_frame/   # 公共配置、协议、dispatcher、router、RPC、数据层
│   ├── *svr/           # 大厅、认证、缓存、好友、匹配、组队等服务
│   ├── component/      # dtmq、distributed_transaction、rank、orbit、共享算法
│   ├── templates/      # 框架维护者使用的代码生成模板
│   ├── tools/          # 代码生成器与离线 RPC 测试工具
│   └── robot/          # Go 压测与模拟客户端
├── install/            # 部署模板与 values 配置
├── resource/           # Excel 表格与资源转换配置
├── project/            # 构建选项与工具
└── third_party/        # 第三方依赖
```
