---
title: 构建
---

# 构建

## Windows（MSVC + vcpkg）

```powershell
cmake -S . -B build_jobs_msvc -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" `
  -DPROJECT_ENABLE_UNITTEST=YES -DPROJECT_ENABLE_SAMPLE=YES
cmake --build build_jobs_msvc --config Debug
```

如需配合 clangd，建议切换 Ninja 生成器以产出 `compile_commands.json`，并保持与
`.vscode/settings.json` 一致（`build_jobs_cmake_tools` 目录）：

```powershell
cmake -S . -B build_jobs_cmake_tools -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 -DPROJECT_ENABLE_UNITTEST=YES
```

## Linux / macOS

```bash
# 自动探测并配置（生成 build_jobs_$(uname -s)）
bash cmake_dev.sh -lus -- -DCRYPTO_USE_OPENSSL=YES

cd build_jobs_*
cmake --build . -- -j4
```

也可以直接手动执行 `cmake -S . -B <BUILD_DIR> [options...]`，使用自己预编译的依赖库。

## 关键 CMake 选项

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `PROJECT_ENABLE_SAMPLE` | OFF | 编译 sample |
| `PROJECT_ENABLE_UNITTEST` | OFF | 编译单元测试 |
| `PROJECT_ENABLE_PRECOMPILE_HEADERS` | ON | 预编译头 |
| `PROJECT_ENABLE_UNITY_BUILD` | OFF | 联合编译（本地开发 + clangd 建议关闭） |
| `ATFRAMEWORK_USE_DYNAMIC_LIBRARY` | Linux 默认 ON | 动态库构建 |
| `PROJECT_SERVER_FRAME_USE_STD_COROUTINE` | 探测后默认 ON | 使用 C++20 协程（否则 libcopp cotask） |
| `PROJECT_SERVER_FRAME_ENABLE_RPC_MOCK` | Debug ON | RPC mock |
| `PROJECT_SANTIZER_USE_ADDRESS/THREAD/...` | OFF | sanitizer |
| `CRYPTO_USE_OPENSSL` / `CRYPTO_USE_MBEDTLS` | — | 加密后端 |

所有服务（echosvr / cachesvr / authsvr / lobbysvr / rank_settlement_svr / orbitsvr / 组件服务 / tools）
都在 `src/CMakeLists.txt` 中无条件构建，没有按服务开关的选项。

构建完成后，可执行文件与运行资源位于 `<BUILD_DIR>/publish/`。

## 增量构建约定

被 CMake target 消费的代码与资源在内容不变时必须保持时间戳：不要无条件 touch 或同内容覆盖；生成规则使用
准确的 `OUTPUT`/`BYPRODUCTS`/`DEPENDS`/`DEPFILE`，内容稳定发布使用 `configure_file` 或
`cmake -E copy_if_different`。
