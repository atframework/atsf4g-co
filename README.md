# atsf4g-co

Service framework for game server using libatbus, libatapp, libcopp and etc.

## Inner Service

+ [atproxy](atframework/service/atproxy) : proxy for logic server to communicate with each other
  + using [etcd][1] for service discovery and online detection.

+ [atgateway](atframework/service/atgateway) : gateway for client to communicate logic server
  + Using DH/ECDH for key establishment(directly send key or no encryption are also supported)
  + Traffic control and handshake timeout available for each client
  + Support for routing switching

## Prepare

1. Install [etcd](https://github.com/coreos/etcd). (It's used for atproxy to connect to each other.)
2. *[opional]* Install [redis](http://redis.io/). (DB services.install it if used)

## Basic Usage

### Build on Windows

You need to prebuilt all dependency libraries such as openssl/mbedtls, libcurl, libuv and so on.
Then run

```bash
mkdir build_jobs_msvc
cd build_jobs_msvc
# run cmake with vcpkg
cmake [SOURCE PATH] -G "Visual Studio 16 2019" -A x64 "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" -DPROJECT_ENABLE_UNITTEST=YES -DPROJECT_ENABLE_SAMPLE=YES -DPROJECT_ENABLE_TOOLS=YES ...

cd build_jobs_msvc

cmake --build . --config Debug
```  

Please see [third_party](third_party) to see which libraries is required.

### Build on Unix like system

```bash
# auto setup cmake
bash cmake_dev.sh [options] ...

# auto setup cmake with openssl, unit test , sample and tools
bash ./cmake_dev.sh -lus -- -DCRYPTO_USE_OPENSSL=YES

# we will generate directory build_jobs_$(basename $(uname -s)) for build and place publish directory
cd build_jobs_*

# just build with make -j4 or ninja -j4
cmake --build . -- -j4
```

Such as sh cmake_dev.sh -su to enable all unit test and samples, or sh cmake_dev.sh -a to use clang-analysis.
You can also directly run cmake [SOURCE PATH] [options...] just like in windows, use your own prebuilt libraries or not.
It depends to you.

All resource and file will be put into ```<BUILD_DIR>/publish``` after compile completed.

### Configure & Run

Configure file is placed on ```<BUILD_DIR>/publish/tools/script/config.conf``` just edit it and set your **etcd server** and ***DB configure***， then run ```<BUILD_DIR>/publish/tools/script/gen_conf.py``` to generate all configure files and scripts for servers. ```restart_all.sh``` 、 ```stop_all.sh``` 、 ```reload_all.sh``` script files will also be generated for easy usage.

```bash
cd publish/tools/script

vim config.conf # edit configure

python3 gen_conf.py

./restart_all.sh
```

```gen_conf.py``` also has options for set some configures instead of using it in config.conf, it's useful when integration our server into other management system. Use ```python3 ./gen_conf.py - --help``` for more help information.

## Developer

### Tools

1. [cmake](https://cmake.org) 3.16 and above 
2. gcc/clang/msvc
3. gdb/lldb/windb
4. git
5. unzip
6. tar
7. autoconf
8. automake
9. p7zip
10. python2/python
11. etc.

### Python modules

1. python-mako/python\*-mako/mingw-w64-x86_64-python\*-mako
2. *[optional]* python-setuptools/python\*-setuptools/mingw-w64-x86_64-python\*-mako
3. *[optional]* python-pip/python\*-pip/mingw-w64-x86_64-python\*-pip 

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
    "--header-insertion=iwyu",
    "-j=8"
  ]
}
```

**MSVC + clangd --query-driver 推荐配置**

当使用 MSVC 工具链时，建议显式配置 `--query-driver` 以便 clangd 正确读取 MSVC 的系统头与内置宏：

```jsonc
{
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

### Framework Code Tree

+ third_party: all dependency third_party libraries
+ atframework: atframework projects and libraries
  + export: exported libraries, used by client
  + atframe_utils: framework utility codes
  + libatbus: communication library used between servers
  + libatapp: server application framework, used to build a specified server type
  + services: inner services of atframework
    1. component: inner services common codes
    2. atproxy: proxy server, used to connect difference service group to each other
    3. atgateway: gateway server, used to manage client connections
+ doc: documents
+ install: all resources and configure templates
+ project: project script, used to detect build environment and generate build scripts
+ sample: sample codes to show usage of some libraries
+ src: all real projects
  + echosvr: the simplest server instance, just send back all data receive from client
  + [others]: other services

### Logic Code Tree

+ src: all real projects
  + server_frame: server common library
    1. config: server configure defines and excel configure data structures
    2. data: game data layer
    3. dispatcher: decide how to deal with each type of messages and manage coroutine tasks
    4. logic: game logic layer
    5. rpc: all remote procedure call APIs, include server to server message, DB message and so on
    6. utility: all shared utility codes
  + tools: tool projects

[1]: https://coreos.com/etcd
