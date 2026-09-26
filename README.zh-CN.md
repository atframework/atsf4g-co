# atsf4g-co

[English](README.md) | [简体中文](README.zh-CN.md)

使用 libatbus、libatapp、libcopp 等构建游戏服务器的服务框架。

## 内部服务

+ [atproxy](atframework/service/atproxy)：逻辑服务器之间相互通信的代理
  + 使用 [etcd][1] 进行服务发现与在线检测。

+ [atgateway](atframework/service/atgateway)：客户端与逻辑服务器通信的网关
  + 使用 DH/ECDH 进行密钥协商（也支持直接发送密钥或不加密）
  + 每个客户端可单独进行流量控制与握手超时设置
  + 支持路由切换

## 准备

1. 安装 [etcd](https://github.com/coreos/etcd)。（用于 atproxy 之间相互连接。）
   + 对于**本地测试**，可以使用 `atframework/libatapp/ci/etcd/setup-etcd` 脚本自动下载并启动一个临时 etcd 实例：
     ```bash
     # Linux / macOS
     bash atframework/libatapp/ci/etcd/setup-etcd.sh start   # 下载并在端口 12379 启动
     export ATAPP_UNIT_TEST_ETCD_HOST="http://127.0.0.1:12379"
     bash atframework/libatapp/ci/etcd/setup-etcd.sh stop    # 完成后停止
     ```
     ```powershell
     # Windows (PowerShell)
     .\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command start
     $env:ATAPP_UNIT_TEST_ETCD_HOST = "http://127.0.0.1:12379"
     .\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command stop
     ```
2. *[可选]* 安装 [redis](http://redis.io/)。（DB 服务，如需使用则安装）

## 基本用法

### 在 Windows 上构建

你需要预先编译所有依赖库，例如 openssl/mbedtls、libcurl、libuv 等。
然后运行

```bash
mkdir build_jobs_msvc
cd build_jobs_msvc
# 使用 vcpkg 运行 cmake
cmake [SOURCE PATH] -G "Visual Studio 16 2019" -A x64 "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" -DPROJECT_ENABLE_UNITTEST=YES -DPROJECT_ENABLE_SAMPLE=YES -DPROJECT_ENABLE_TOOLS=YES ...

cd build_jobs_msvc

cmake --build . --config Debug
```  

需要哪些依赖库请参见 [third_party](third_party)。

### 在类 Unix 系统上构建

```bash
# 自动配置 cmake
bash cmake_dev.sh [options] ...

# 使用 openssl、单元测试、样例和工具自动配置 cmake
bash ./cmake_dev.sh -lus -- -DCRYPTO_USE_OPENSSL=YES

# 会生成目录 build_jobs_$(basename $(uname -s)) 用于构建并放置 publish 目录
cd build_jobs_*

# 直接用 make -j4 或 ninja -j4 构建
cmake --build . -- -j4
```

例如 sh cmake_dev.sh -su 可启用全部单元测试和样例，或 sh cmake_dev.sh -a 使用 clang 静态分析。
你也可以像在 Windows 上一样直接运行 cmake [SOURCE PATH] [options...]，是否使用自己预编译的库由你决定。

编译完成后，所有资源与文件会放入 ```<BUILD_DIR>/publish```。单元测试可执行文件放入 ```PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY```（默认 ```<BUILD_DIR>/test```），样例放入 ```PROJECT_SAMPLE_RUNTIME_OUTPUT_DIRECTORY```（默认 ```<BUILD_DIR>/sample```）。

### 配置与运行

配置文件位于 ```<BUILD_DIR>/publish/tools/script/config.conf```，编辑它并设置你的 **etcd 服务器** 与 ***DB 配置***，然后运行 ```<BUILD_DIR>/publish/tools/script/gen_conf.py``` 为各服务器生成所有配置文件与脚本。同时还会生成 ```restart_all.sh```、```stop_all.sh```、```reload_all.sh``` 等脚本以便使用。

```bash
cd publish/tools/script

vim config.conf # 编辑配置

python3 gen_conf.py

./restart_all.sh
```

```gen_conf.py``` 也提供了直接设置部分配置项的选项，而不必写入 config.conf；将服务器集成到其他管理系统时很有用。使用 ```python3 ./gen_conf.py - --help``` 获取更多帮助信息。

## 开发者

### 增量构建卫生

被 CMake 目标消费的代码和资源，在内容未变化时必须保持时间戳不变。不要无条件 touch 它们或覆盖写入相同内容，无论它们是生成的、拷贝的还是非手写的。用准确的 `OUTPUT`、`BYPRODUCTS`、`DEPENDS`、`DEPFILE` 关系描述生成过程；使用 `configure_file`、`file(CONFIGURE)`、`file(GENERATE)`，或临时输出加 `cmake -E copy_if_different` 实现内容稳定的发布。只有当某个专用 stamp 文件不是被编译或被打包的输入时，才可以对其 touch。

### 工具

1. [cmake](https://cmake.org) 3.16 及以上
2. gcc/clang/msvc
3. gdb/lldb/windb
4. git
5. unzip
6. tar
7. autoconf
8. automake
9. p7zip
10. python2/python
11. 等等

### Python 模块

1. python-mako/python\*-mako/mingw-w64-x86_64-python\*-mako
2. *[可选]* python-setuptools/python\*-setuptools/mingw-w64-x86_64-python\*-mako
3. *[可选]* python-pip/python\*-pip/mingw-w64-x86_64-python\*-pip

### Clangd 配置参考

本仓库当前的 VS Code 配置已启用 clangd，并且固定使用 `build_jobs_cmake_tools` 作为编译数据库目录（由 CMake 生成 `compile_commands.json`）。建议保持与当前设置一致：

**.clangd（可选，若希望在非 VS Code 环境复用配置）**

```yaml
CompileFlags:
  CompilationDatabase: build_jobs_cmake_tools

Index:
  Background: Build

Diagnostics:
  UnusedIncludes: Strict
```

**VS Code settings**

```json
{
  "C_Cpp.intelliSenseEngine": "disabled",
  "clangd.enable": true,
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build_jobs_cmake_tools",
    "--background-index",
    "--clang-tidy",
    "--completion-style=detailed",
    "--header-insertion=Never",
    "-j=8"
  ]
}
```

**MSVC + clangd --query-driver 推荐配置**

当使用 MSVC 工具链时，建议显式配置 `--query-driver` 以便 clangd 正确读取 MSVC 的系统头与内置宏：

```jsonc
{
  // Visual Studio * 的Generator不会生成 compile_commands.json, 所以需要切到 Ninja 生成
  "cmake.generator": "Ninja",
  "clangd.arguments": [
    // 使用环境变量（推荐，VS 开发者命令行会注入 VCToolsInstallDir）
    "--query-driver=${env:VCToolsInstallDir}bin/Hostx64/x64/cl.exe",

    // 通配符版本（多个版本时会选择第一个匹配项）
    "--query-driver=C:/Program Files/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe"
  ]
}
```

**显式指定 C++ 标准（CMake 配置）**

如果需要固定 `__cplusplus` 版本，可通过 CMake 统一指定标准（示例：C++20）：

```jsonc
{
  "cmake.configureSettings": {
    "CMAKE_CXX_STANDARD": "20",
    "CMAKE_CXX_STANDARD_REQUIRED": "ON"
  }
}
```

说明：只有在 MSVC 下 clangd 不识别 `-std:c++latest` 和 `/std:c++latest` 时，才需要使用以上方式固定标准。使用 `CMAKE_CXX_STANDARD` 通常兼容性更好；`--query-driver` 不影响单独打开的 `.h` 文件，仅影响带编译命令的翻译单元。两者可二选一使用。

### 框架代码树

+ third_party：所有依赖的第三方库
+ atframework：atframework 工程与库
  + export：导出的库，供客户端使用
  + atframe_utils：框架工具代码
  + libatbus：服务器之间使用的通信库
  + libatapp：服务器应用框架，用于构建指定类型的服务器
  + services：atframework 的内部服务
    1. component：内部服务的公共代码
    2. atproxy：代理服务器，用于连接不同的服务组
    3. atgateway：网关服务器，用于管理客户端连接
+ doc：文档
+ install：部署资源与 Go 模板配置模板（`*.yaml.tpl` -> YAML，`*.sh.tpl` -> shell，
  `*.ps1.tpl` -> PowerShell 脚本）
+ project：工程脚本，用于探测构建环境并生成构建脚本
+ sample：展示部分库用法的样例代码
+ src：所有真实工程
  + echosvr：最简单的服务器实例，只把从客户端收到的数据原样发回
  + [others]：其他服务

### 逻辑代码树

+ src：所有真实工程
  + server_frame：服务器公共库
    1. config：服务器配置定义与 Excel 配置数据结构
    2. data：游戏数据层
    3. dispatcher：决定如何处理各类消息并管理协程任务
    4. logic：游戏逻辑层
    5. rpc：所有远程过程调用 API，包括服务器间消息、DB 消息等
    6. utility：所有共享工具代码
  + tools：工具工程

## FAQ

以下命令中的 `<BUILD_DIR>` 指你的构建目录（例如本仓库 VS Code 配置固定使用的 `build_jobs_cmake_tools`，或 `cmake_dev.sh` 生成的 `build_jobs_*`）。

### 如何清理 `cmake --build` 生成的预编译头(PCH)产物？

预编译头的编译产物（`cmake_pch.*.pch` / `.gch` / `.obj` / `.o`）分散在各目标的 `CMakeFiles/<target>.dir/` 下。仓库提供了专用清理目标，按需手动执行即可：

```bash
cmake --build <BUILD_DIR> --target cleanup-precompile-headers
```

该目标只删除已编译的 PCH 产物，保留 PCH 桩源码（`cmake_pch.hxx` / `cmake_pch.cxx`）以及 clangd 的 `.cache` 索引分片，因此下次构建会重新生成 PCH，无需重新执行 CMake configure。

也可以不经过构建目标，直接以脚本模式运行底层清理脚本（路径相对仓库根目录）：

```bash
cmake -DPROJECT_PCH_CLEANUP_DIR=<BUILD_DIR> -P project/cmake/CleanupPrecompileHeaders.cmake
```

### 如何清理生成的协议文件与生成代码？

所有生成产物都集中在 `<BUILD_DIR>/_generated/` 下，主要包含：

+ `_generated/pbdesc/`：protobuf 描述符文件（`config.pb`、`serverframe_all.pb` 等）
+ `_generated/server_frame-protocol/`：由 `.proto` 生成的 protobuf C++ 源码（`protocol-common`、`protocol-config` 等）
+ `_generated/server_frame-config/`：由 Excel 配置 + Mako 模板生成的配置加载代码（`config_manager`、`config_easy_api` 等）
+ `_generated/generate-for-pb/`：由模板生成的 RPC/task 代码
+ `_generated/tools/`、`_generated/pch/` 等：生成的脚本与 PCH 工具目录

这些产物由带有正确 `DEPENDS` 的构建期自定义命令生成，改动 `.proto`/模板/Excel 后 `cmake --build` 会自动重新生成，通常无需手动清理。

若只想重建某一类产物，直接构建对应目标即可（无需清空整个 `_generated`）：

```bash
cmake --build <BUILD_DIR> --target protocol            # 重新生成协议 C++ 源码
cmake --build <BUILD_DIR> --target config-loader       # 重新生成 Excel 配置加载代码
cmake --build <BUILD_DIR> --target serverframe_all_pb  # 重新生成 protobuf 描述符
```

若要强制完整重建全部生成产物，使用专用的 `cleanup-generated-sources` 目标清理（无需手动删除目录），然后重新配置再构建。因为配置代码的产物清单以及 `server_frame_build_feature.h/.cpp` 等文件是在 configure 阶段（`configure_file`/protoc 枚举）生成的，清理后需要重新 configure 才能恢复：

```bash
cmake --build <BUILD_DIR> --target cleanup-generated-sources   # 删除整个 <BUILD_DIR>/_generated 目录树
cmake <BUILD_DIR>                                              # 重新配置，恢复 configure 阶段的生成步骤
cmake --build <BUILD_DIR>                                      # 重新构建，触发构建期重新生成
```

[1]: https://coreos.com/etcd
