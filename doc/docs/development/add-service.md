---
title: 新增服务
---

# 新增服务

## 快速上手：复制标准服务

使用 `src/authsvr/` 作为小型 CS RPC 服务模板；SS RPC 与独立 SDK 可参照
`src/lobbysvr/protocol/CMakeLists.txt`。复制现有声明即可，不需要编写生成模板。
`echosvr` 是原始消息回显示例，不包含完整的业务 RPC 装配。

1. 将 `src/authsvr/` 复制为 `src/examplesvr/`，保留 `protocol/`、`service/app/` 和业务目录结构。
   替换服务名、main 文件名、配置 message/头文件、配置目标名和导出宏；不要复制
   `*.atfw.gen.*` 和原服务的业务 action。
2. 在公共 `com.protocol.proto` 定义新的 `ExamplesvrClientService` 及其消息，
   按[RPC 上手](add-rpc-task)填写方法声明。在新目录的已有生成声明中，将
   `AuthsvrClientService` 改为 `ExamplesvrClientService`。
3. 在复制的 main 中更新配置段与环境变量前缀，以及
   `register_handles_for_examplesvrclientservice()` 和对应生成头文件。
   保留原有公共模块/dispatcher 初始化流程。新增业务模块时在其后挂载。
4. 在 `src/CMakeLists.txt` 加入 `add_subdirectory(examplesvr)`；重新配置/构建，
   填充生成的 action，再次构建。
5. 复制 `install/cloud-native/charts/authsvr/` 为新服务 chart，替换名称和配置段。
   在 `src/server_frame/config/include/config/extern_service_types.h` 的
   `logic_service_type` 中分配新类型，在新 chart 的 `values.yaml` 填写一致的
   `proc_name`、`type_id`、`type_name`；在所用 values 的
   `non_cloud_native/deploy.yaml` 登记实例。
6. 按[运行与部署](../getting-started/run-deploy)生成配置并启动新实例，检查发现注册和一次正常 RPC。

服务或配置名称不能与原服务重复。类型枚举、values 映射和进程布局必须一致。

## 快速上手：新增 SS RPC 服务声明

已有标准服务要增加一组独立 SS RPC 时，沿用其协议目标，把 service 名和输出位置填入已有 helper。
以大厅服为例，协议文件追加到 `lobbysvr-protocol` 的 `PROTOCOLS`，生成声明为：

```cmake
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.ExampleService"
  "${LOBBYSVR_ROOT_DIR}/service"
  TASK_PATH_PREFIX "logic"
  HANDLE_PATH_PREFIX "app"
  PROJECT_NAMESPACE "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR "${LOBBYSVR_ROOT_DIR}/sdk"
  RPC_DLLEXPORT_DECL LOBBY_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS "lobbysvr-protocol"
  INCLUDE_HEADERS "protocol/pbdesc/example_service.pb.h")
```

在服务和 SDK 已有声明的 `GENERATED_FLOW_NAMES` 中加入同一个 service 全名，
在 main 注册生成的 `register_handles_for_exampleservice()`。
之后给此 service 增加方法只需修改 proto 和业务 action。
独立 SDK 的头文件/源文件清单沿用已有 SDK 声明补充新接口文件。

## 定制与详细设计

服务端公共装配见 `src/server_frame/logic/logic_server_setup.h`；
生成规则见[RPC 与代码生成](../architecture/rpc-codegen)。
服务实现会由 helper 组织为可执行文件与 private 静态库，入口目录默认 `app/`。
测试应使用工程测试 helper；涉及服务端 RPC 的测试见[RPC 单元测试](rpc-unit-test)。

需要自定义编译、生成或部署方式时，再阅读 `src/service-functions.cmake`、
`src/tools/generate_for_pb_utility.cmake` 和 `install/cloud-native/charts/libapp/`。
