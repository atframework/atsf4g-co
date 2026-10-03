---
title: 环境准备
---

# 环境准备

## 平台与语言要求

支持 Windows、Linux 和 macOS。项目代码要求 **C++14**；使用传统有栈协程时不需要 C++20。
选择标准协程后端时需 C++20 及标准协程支持，切换方法见[构建](build#coroutine-backend)。

第三方依赖有各自的编译器与语言标准要求，较新版本可能需要更高标准。
工程会按工具链选择依赖版本与可用特性；不要把本地 C++20 配置当作项目代码的最低要求。
仅设置 `CMAKE_CXX_STANDARD=14` 不能保证任意版本的依赖都兼容 C++14。

## 工具链

| 工具 | 用途与要求 |
| --- | --- |
| CMake ≥ 3.24 | 配置、生成代码与构建 |
| MSVC / GCC / Clang / AppleClang | 支持 C++14；标准协程后端另外要求 C++20 协程能力 |
| Python 3 | 协议与配置代码生成；依赖由工程工具链配置 |
| Java Runtime | xresloader 导出 Excel 资源，配置时会检查 |
| PowerShell 7+ | Windows 构建和运行辅助脚本 |
| Bash | Linux/macOS 构建与运行辅助脚本 |
| git | 获取项目与框架子模块 |
| Go | robot 模拟客户端；版本与构建由 robot 工具配置 |
| vcpkg（可选） | 使用已有 Windows 依赖时可传入其 toolchain 文件 |

第三方依赖由 cmake-toolset ports 管理（`third_party/Repository.cmake`），也可使用已有依赖安装目录。
按[构建指南](build)完成首次配置后，日常开发复用构建目录。

## 运行时依赖

### etcd

标准服务通过 etcd 注册与发现，atproxy 也使用它检测在线服务。本地环境可使用发布目录中的
`tools/script/start_local_test_env.sh/.ps1` 同时启动 etcd 和 Redis，见[运行与部署](run-deploy)。

仅需临时 etcd 测试实例时：

```bash
bash atframework/libatapp/ci/etcd/setup-etcd.sh start
```

```powershell
pwsh -NoLogo -NoProfile -File atframework/libatapp/ci/etcd/setup-etcd.ps1 -Command start
```

该脚本默认使用端口 12379。`ATAPP_UNIT_TEST_ETCD_HOST` 是 libatapp 测试使用的环境变量，
业务服务的 etcd 地址应在部署 values 中配置，两者不会自动同步。

### Redis

使用用户数据、缓存、DTMQ、事务等持久化能力的服务需要 Redis。
框架支持 cluster 和 raw/sentinel 连接；按所用服务选择配置。
原始消息回显示例不要求启用完整数据层。

## 目录约定

`<BUILD_DIR>` 代表实际构建目录。优先使用 `.vscode/settings.json` 中的
`cmake.buildDirectory`；未设置时参考 clangd 的 `--compile-commands-dir` 或已有构建目录，
本工作区使用 `build_jobs_cmake_tools`。

服务和运行资源在 `<BUILD_DIR>/publish/`；单元测试默认在 `<BUILD_DIR>/test/`，
sample 默认在 `<BUILD_DIR>/sample/`。Linux/macOS 的 `cmake_dev.sh` 也可创建平台对应的构建目录。
