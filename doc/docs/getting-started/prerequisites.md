---
title: 环境准备
---

# 环境准备

## 工具链

| 工具 | 要求 |
| --- | --- |
| CMake | ≥ 3.24 |
| 编译器 | MSVC 2022（Windows）/ GCC 10+ / Clang 12+（Linux/macOS） |
| Python | 3.x（含 mako：`pip install mako`，代码生成需要） |
| vcpkg | Windows 下提供 openssl/mbedtls、libcurl、libuv 等预编译依赖 |
| git | 子模块初始化（`atframework/Repository.cmake` 自动处理） |

第三方依赖通过 cmake-toolset ports 机制下载构建（见 `third_party/Repository.cmake`），支持 GitHub 镜像。

## 运行时依赖

### etcd（必需）

atproxy 使用 etcd 做服务发现与在线检测。本地测试可用一键脚本启动临时实例：

```bash
# Linux / macOS（端口 12379）
bash atframework/libatapp/ci/etcd/setup-etcd.sh start
export ATAPP_UNIT_TEST_ETCD_HOST="http://127.0.0.1:12379"
```

```powershell
# Windows
.\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command start
$env:ATAPP_UNIT_TEST_ETCD_HOST = "http://127.0.0.1:12379"
```

### Redis（可选，按需）

数据层（`db_msg_dispatcher`、路由对象缓存、dtmq 频道记录等）使用 Redis（支持 cluster 与 sentinel/raw
两种连接模式）。用到 DB 的服务需要提前准备。

## 目录约定

- 构建产物统一输出到 `<BUILD_DIR>/publish/`。
- Windows 下推荐构建目录 `build_jobs_cmake_tools`（与 `.vscode/settings.json` 中的 clangd 配置一致）。
- Linux 下 `cmake_dev.sh` 自动生成 `build_jobs_$(uname -s)`。
