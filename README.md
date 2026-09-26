# atsf4g-co

[English](README.md) | [简体中文](README.zh-CN.md)

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
   + For **local testing**, you can use `atframework/libatapp/ci/etcd/setup-etcd` scripts to auto-download and start a temporary etcd instance:
     ```bash
     # Linux / macOS
     bash atframework/libatapp/ci/etcd/setup-etcd.sh start   # download + start on port 12379
     export ATAPP_UNIT_TEST_ETCD_HOST="http://127.0.0.1:12379"
     bash atframework/libatapp/ci/etcd/setup-etcd.sh stop    # stop when done
     ```
     ```powershell
     # Windows (PowerShell)
     .\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command start
     $env:ATAPP_UNIT_TEST_ETCD_HOST = "http://127.0.0.1:12379"
     .\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command stop
     ```
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

All resource and file will be put into ```<BUILD_DIR>/publish``` after compile completed. Unit test executables are put into ```PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY```(default ```<BUILD_DIR>/test```) and samples are put into ```PROJECT_SAMPLE_RUNTIME_OUTPUT_DIRECTORY```(default ```<BUILD_DIR>/sample```).

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

### Incremental build hygiene

Code and resources consumed by CMake targets must keep their timestamps when their content is unchanged. Do not
unconditionally touch them or overwrite identical content, whether they are generated, copied, or non-handwritten.
Describe generation with accurate `OUTPUT`, `BYPRODUCTS`, `DEPENDS`, and `DEPFILE` relationships; use
`configure_file`, `file(CONFIGURE)`, `file(GENERATE)`, or temporary output plus `cmake -E copy_if_different` for
content-stable publication. A dedicated stamp file may be touched only when it is not a compiled or packaged input.

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

### Clangd configuration reference

The current VS Code configuration in this repository enables clangd and pins `build_jobs_cmake_tools` as the compilation database directory (CMake generates `compile_commands.json` there). It is recommended to keep it consistent with the current settings:

**.clangd (optional, if you want to reuse the configuration outside VS Code)**

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

**MSVC + clangd --query-driver recommended configuration**

When using the MSVC toolchain, it is recommended to explicitly configure `--query-driver` so that clangd correctly reads MSVC's system headers and built-in macros:

```jsonc
{
  // The "Visual Studio *" generators do not produce compile_commands.json, so switch to the Ninja generator
  "cmake.generator": "Ninja",
  "clangd.arguments": [
    // Use an environment variable (recommended; the VS developer command prompt injects VCToolsInstallDir)
    "--query-driver=${env:VCToolsInstallDir}bin/Hostx64/x64/cl.exe",

    // Wildcard version (the first match is chosen when multiple versions exist)
    "--query-driver=C:/Program Files/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe"
  ]
}
```

**Explicitly specify the C++ standard (CMake configuration)**

If you need to pin the `__cplusplus` version, you can specify the standard uniformly through CMake (example: C++20):

```jsonc
{
  "cmake.configureSettings": {
    "CMAKE_CXX_STANDARD": "20",
    "CMAKE_CXX_STANDARD_REQUIRED": "ON"
  }
}
```

Note: you only need to pin the standard this way when clangd under MSVC does not recognize `-std:c++latest` and `/std:c++latest`. Using `CMAKE_CXX_STANDARD` is usually more compatible; `--query-driver` does not affect standalone `.h` files opened on their own, only translation units that carry compile commands. Use either one of the two.

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
+ install: deployment resources and Go-template configure templates (`*.yaml.tpl` -> YAML, `*.sh.tpl` -> shell,
  `*.ps1.tpl` -> PowerShell script)
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

## FAQ

In the commands below, `<BUILD_DIR>` refers to your build directory (for example `build_jobs_cmake_tools`, which this repository's VS Code configuration pins, or the `build_jobs_*` directory generated by `cmake_dev.sh`).

### How do I clean up the precompiled header (PCH) artifacts produced by `cmake --build`?

The compiled PCH artifacts (`cmake_pch.*.pch` / `.gch` / `.obj` / `.o`) are scattered under each target's `CMakeFiles/<target>.dir/`. The repository provides a dedicated cleanup target that you can run manually on demand:

```bash
cmake --build <BUILD_DIR> --target cleanup-precompile-headers
```

This target only removes the compiled PCH artifacts and preserves the PCH stub sources (`cmake_pch.hxx` / `cmake_pch.cxx`) and the clangd `.cache` index shards, so the next build regenerates the PCH without re-running CMake configure.

You can also run the underlying cleanup script directly in script mode without going through a build target (the path is relative to the repository root):

```bash
cmake -DPROJECT_PCH_CLEANUP_DIR=<BUILD_DIR> -P project/cmake/CleanupPrecompileHeaders.cmake
```

### How do I clean up the generated protocol files and generated code?

All generated artifacts are collected under `<BUILD_DIR>/_generated/`, mainly including:

+ `_generated/pbdesc/`: protobuf descriptor files (`config.pb`, `serverframe_all.pb`, etc.)
+ `_generated/server_frame-protocol/`: protobuf C++ sources generated from `.proto` (`protocol-common`, `protocol-config`, etc.)
+ `_generated/server_frame-config/`: config loader code generated from Excel configs + Mako templates (`config_manager`, `config_easy_api`, etc.)
+ `_generated/generate-for-pb/`: RPC/task code generated from templates
+ `_generated/tools/`, `_generated/pch/`, etc.: generated scripts and the PCH tool directory

These artifacts are produced by build-time custom commands with correct `DEPENDS`. After changing `.proto`/templates/Excel, `cmake --build` regenerates them automatically, so manual cleanup is usually unnecessary.

If you only want to rebuild one category of artifacts, just build the corresponding target (no need to wipe the whole `_generated`):

```bash
cmake --build <BUILD_DIR> --target protocol            # regenerate protocol C++ sources
cmake --build <BUILD_DIR> --target config-loader       # regenerate Excel config loader code
cmake --build <BUILD_DIR> --target serverframe_all_pb  # regenerate protobuf descriptors
```

To force a full rebuild of all generated artifacts, use the dedicated `cleanup-generated-sources` target instead of deleting the directory by hand, then reconfigure and rebuild. Because the config-code output list and files such as `server_frame_build_feature.h/.cpp` are generated during the configure stage (`configure_file`/protoc enumeration), a reconfigure is required after the cleanup to restore them:

```bash
cmake --build <BUILD_DIR> --target cleanup-generated-sources   # remove the whole <BUILD_DIR>/_generated tree
cmake <BUILD_DIR>                                              # reconfigure to restore the configure-stage generation steps
cmake --build <BUILD_DIR>                                      # rebuild to trigger build-time regeneration
```

[1]: https://coreos.com/etcd
