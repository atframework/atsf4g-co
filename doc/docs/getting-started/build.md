---
title: 构建
---

# 构建

## 快速上手

首次构建前完成[环境准备](prerequisites)。以下目录名是示例；已有构建目录时沿用其 generator、
工具链和选项。

### Windows

在 MSVC 开发环境的 PowerShell 7 中执行：

```powershell
cmake -S . -B build_jobs_msvc -G "Visual Studio 17 2022" -A x64
cmake --build build_jobs_msvc --config Debug --parallel 4
```

需要使用 vcpkg 时，在首次配置命令中追加
`"-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake"`。
它是可选的依赖入口。需要 clangd 的编译数据库时，使用 Ninja：

```powershell
cmake -S . -B build_jobs_cmake_tools -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build_jobs_cmake_tools --parallel 4
```

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

也可使用 `bash cmake_dev.sh` 自动选择环境；不要给已有构建目录换 generator。
构建完成后按[运行与部署](run-deploy)启动服务。

## 一键切换协程后端 {#coroutine-backend}

同一工程通过 `PROJECT_SERVER_FRAME_USE_STD_COROUTINE` 切换后端，重新配置并构建后生效。
使用 `RPC_AWAIT_*`、`RPC_RETURN_*` 和框架结果类型的业务代码无需改写。

```bash
# Traditional stackful coroutines
cmake -S . -B <BUILD_DIR> -DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=OFF
cmake --build <BUILD_DIR> --config Debug --parallel 4

# C++20 standard coroutines
cmake -S . -B <BUILD_DIR> -DCMAKE_CXX_STANDARD=20 -DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=ON
cmake --build <BUILD_DIR> --config Debug --parallel 4
```

ON 依赖编译器的标准协程检测结果；不支持时该选项被强制关闭。
有栈后端支持 C++14 项目代码。若需要整个构建采用 C++14，可在首次配置时设置
`-DCMAKE_CXX_STANDARD=14 -DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=OFF`，
并选择兼容该标准的第三方依赖。
本地 VS Code 配置使用 C++20 是后端选择，不代表最低要求。

## 常用构建选项

| 选项 | 默认 | 用途 |
| --- | --- | --- |
| `PROJECT_ENABLE_SAMPLE` | OFF | 构建 sample |
| `PROJECT_ENABLE_UNITTEST` | 已定义 `BUILD_TESTING` 时跟随它，否则 Debug ON、其他 OFF | 构建单元测试 |
| `PROJECT_ENABLE_PRECOMPILE_HEADERS` | ON | 预编译头 |
| `PROJECT_ENABLE_UNITY_BUILD` | OFF | 联合编译 |
| `ATFRAMEWORK_USE_DYNAMIC_LIBRARY` | 默认 ON（macOS 除外；显式设置 `BUILD_SHARED_LIBS` 时跟随它） | 动态库 |
| `PROJECT_SERVER_FRAME_USE_STD_COROUTINE` | 检测支持时 ON，否则 OFF | 标准/有栈协程后端 |
| `PROJECT_SERVER_FRAME_ENABLE_RPC_MOCK` | Debug ON | RPC mock |
| `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` | 随单元测试开关启用 | 离线 RPC 测试 hook |
| `CRYPTO_USE_OPENSSL` / `CRYPTO_USE_MBEDTLS` | 按配置选择 | 加密后端 |

业务服务、组件服务和常规工具目录在 `src/CMakeLists.txt` 中登记，已有这些服务没有逐服务构建开关。
robot 有自己的 Go 构建流程；使用测试时见[测试指南](../development/testing)。

## 构建产物

服务与运行资源位于 `<BUILD_DIR>/publish/`。
单元测试默认位于 `<BUILD_DIR>/test/`，由 `PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY` 控制；
sample 默认位于 `<BUILD_DIR>/sample/`，由 `PROJECT_SAMPLE_RUNTIME_OUTPUT_DIRECTORY` 控制。
日常扩展的生成步骤见[开发快速上手](../development/overview)。

## 定制与增量构建

需要改构建行为时参照 `project/cmake/` 与各模块的声明。
被 target 消费的代码和资源在内容不变时必须保持时间戳；生成规则声明准确的
`OUTPUT`、`BYPRODUCTS`、`DEPENDS` 与 `DEPFILE`，内容稳定发布可用
`configure_file` 或 `cmake -E copy_if_different`。

### 按目标重新生成

只需更新一类生成物时，可直接构建对应目标：

```bash
cmake --build <BUILD_DIR> --config Debug --target protocol
cmake --build <BUILD_DIR> --config Debug --target config-loader
cmake --build <BUILD_DIR> --config Debug --target serverframe_all_pb
```

需要重建全部生成物时，使用工程提供的 `cleanup-generated-sources`，再正常构建。
此目标只清理已登记的构建期生成产物，保留配置期生成的脚本和配置；不要手工删除整个 `_generated/`。

```bash
cmake --build <BUILD_DIR> --config Debug --target cleanup-generated-sources
cmake --build <BUILD_DIR> --config Debug --parallel 4
```
