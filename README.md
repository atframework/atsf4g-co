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
