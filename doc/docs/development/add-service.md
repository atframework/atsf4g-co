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
   在新 chart 的 `values.yaml` 中分配唯一的 `type_id`，填写对应的 `proc_name` 和 `type_name`。
   需要按类型发现服务时，在 `src/server_frame/protocol/private/protocol/config/svr.hpa.config.proto`
   中添加与 `type_name` 一致的发现别名。在所用 values 的
   `non_cloud_native/deploy.yaml` 登记实例。
6. 按[运行与部署](../getting-started/run-deploy)生成配置并启动新实例，检查发现注册和一次正常 RPC。

为服务和配置分配唯一名称，并保持 values、发现别名和进程布局一致。

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

## 交易行服务组

`src/tradesvr/` 下的 index、matchmaking、order、price、search 进程分别使用 `protocol/<role>/`
中的 SS RPC 和配置协议。`sdk::trade-sdk-<role>` 提供 `rpc::trade_<role>` 下生成的客户端 API。
各 SDK 在 `src/tradesvr/sdk/` 对应子目录的 `CMakeLists.txt` 中独立声明。
角色 SDK 依赖 `sdk::trade-common-sdk`，公共算法位于 `src/tradesvr/sdk/common/trade/`。
各进程直接链接的 SDK 见 `src/tradesvr/service/CMakeLists.txt`。
各进程在自己的 `service/<role>/app/trade_<role>_main.cpp` 中定义 `main_module`。
该模块负责注册服务端 handler，也是该进程的生命周期扩展点。
当前请求/响应 RPC 返回 `EN_ERR_NOT_IMPLEMENTED`，单向 RPC 沿用无响应语义。
业务 action 位于各自的 `service/<role>/logic/action/`。

每个进程在 `install/cloud-native/charts/tradesvr-<role>/` 下有独立 chart。默认本地部署的第 9 组
配置各角色的实例。类型 ID 跟随 `svr.hpa.config.proto` 中 `logic_discovery_selector_cfg`
的字段编号。order、matchmaking、index、search、price 分别使用 51、52、53、54、55。

在 `install/cloud-native/values/default/modules/trade.yaml` 维护 `trade.service.common` 和
`trade.service.<role>`。渲染时各角色读取自己的配置，将公共配置写入 `shared_setting`。
`trade.hpa.<role>` 提供各角色的 HPA 参数。进程读取渲染后的 `tradesvr_<role>` 配置段，
环境变量前缀为 `ATAPP_TRADESVR_<ROLE>`。

order 的自定义规则配置在 `trade.hpa.order.rule.custom`。
`tgf_hpa_rule_search_cpu_rate` 保留扩容阈值 300 和缩容阈值 250，需要监控后端提供对应时间序列。
`product_sales_order` 定义订单数的分组聚合，样本采集需接入订单业务实现。

## 定制与详细设计

服务端公共装配见 `src/server_frame/logic/logic_server_setup.h`；
生成规则见[RPC 与代码生成](../architecture/rpc-codegen)。
服务实现会由 helper 组织为可执行文件与 private 静态库，入口目录默认 `app/`。
测试应使用工程测试 helper；涉及服务端 RPC 的测试见[RPC 单元测试](rpc-unit-test)。

需要自定义编译、生成或部署方式时，再阅读 `src/service-functions.cmake`、
`src/tools/generate_for_pb_utility.cmake` 和 `install/cloud-native/charts/libapp/`。
