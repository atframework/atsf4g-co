# atsf4g-co

[English](README.md) | [简体中文](README.zh-CN.md)

**atsf4g-co**（AT Service Framework for Game - Coroutine）是基于 libatbus、libatapp、libcopp
等 atframework 组件的游戏服务器框架。项目代码要求 **C++14**，支持 **Windows、Linux 和 macOS**。

支持 **C++20 标准协程**和 **传统有栈协程**，通过 `PROJECT_SERVER_FRAME_USE_STD_COROUTINE`
一键切换。使用框架协程接口的业务代码无需修改；C++20 后端需要标准协程工具链。
第三方依赖的版本与工具链要求需单独核对。

提供客户端网关 atgateway、跨服代理 atproxy、服务发现、Redis 数据层、路由缓存与 OpenTelemetry。
可复用组件和服务包括 **DTMQ、分布式事务、排行榜、好友、匹配、组队**。
**Orbit 是针对 Unreal Engine（UE）的 Dedicated Server（DS）管理解决方案**。

## 快速开始

完整文档：[中文文档](https://atframe.work/) · [本地文档源文件](doc/docs/intro.md)。

1. 按[环境准备](doc/docs/getting-started/prerequisites.md)安装 CMake ≥ 3.24、C++ 工具链、
   Python 3 和 Java Runtime；Windows 辅助脚本使用 PowerShell 7+。
2. 构建工程。下面的命令适用于 Linux/macOS 或已准备好的 MSVC 开发环境：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

Windows 的 Visual Studio generator、可选 vcpkg 和协程切换命令见[构建指南](doc/docs/getting-started/build.md)。
已有工作区应沿用其构建目录、generator 与工具链；本工作区 VS Code 使用 `build_jobs_cmake_tools`。

3. 在 `install/cloud-native/values/personal/` 填写本地覆盖配置并构建，确认 etcd/Redis 地址与进程布局。
   在发布目录生成实例配置并启动：

```bash
cd build/publish/tools/script
bash start_local_test_env.sh
bash generate_config.sh
cd ../..
bash start_all.sh
```

Windows 使用同目录的 `.ps1` 脚本，通过 `pwsh -NoLogo -NoProfile -File <脚本>` 执行。
已有外部 etcd/Redis 时省略临时依赖启动。详情见[运行与部署](doc/docs/getting-started/run-deploy.md)。

## 日常开发

已有生成流程覆盖 RPC、协议、数据库和 Excel。使用者修改声明配置并实现业务逻辑即可；
新增服务或组件复制已有结构、填写声明，无需研究 CMake helper 的实现或编写 Mako 模板。

| 任务 | 上手入口 |
| --- | --- |
| RPC / task action | [新增与修改 RPC](doc/docs/development/add-rpc-task.md) |
| 服务 / 组件 | [新增服务](doc/docs/development/add-service.md)、[组件接入](doc/docs/development/add-component.md) |
| Excel / 服务端配置 | [Excel](doc/docs/development/excel-config.md)、[服务端配置](doc/docs/development/server-config.md) |
| 协议 / 数据库表 | [协议](doc/docs/development/add-protocol.md)、[数据库表](doc/docs/development/add-db-table.md) |
| 可复用模块 | [组件总览与各模块快速上手](doc/docs/components/overview.md) |
| 验证 | [单元测试](doc/docs/development/testing.md)、[离线 RPC 测试](doc/docs/development/rpc-unit-test.md) |

[开发快速上手](doc/docs/development/overview.md)汇总最小操作路径；
需要定制底层功能时再看[架构设计](doc/docs/architecture/overview.md)。

## 构建产物与开发工具

运行资源与服务在 `<BUILD_DIR>/publish/`，测试默认在 `<BUILD_DIR>/test/`，
sample 默认在 `<BUILD_DIR>/sample/`。
clangd 使用 Ninja 生成的 `compile_commands.json`；VS Code 的
`--compile-commands-dir` 应与实际构建目录一致。

内容未变化的代码和资源必须保持时间戳。生成规则声明准确的
`OUTPUT`、`BYPRODUCTS`、`DEPENDS` 与 `DEPFILE`；内容稳定发布使用
`configure_file` 或 `cmake -E copy_if_different`。

仓库结构与各服务职责见[项目简介](doc/docs/intro.md)和[服务总览](doc/docs/services/overview.md)。
