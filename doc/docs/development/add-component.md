---
title: 组件接入与新增
---

# 组件接入与新增

## 快速上手：使用已有组件

1. 从[组件总览](../components/overview)选择组件，按其快速上手准备服务实例与配置。
2. 在消费方已有 `project_service_declare_instance` 声明中填写 SDK 依赖。
   例如使用 DTMQ 的服务将 `dtmq-proxy-sdk` 追加到 `USE_COMPONENTS`：
   `USE_COMPONENTS "dtmq-proxy-sdk"`。好友与匹配等位于 `src/*svr/`，
   对应 SDK 填入 `USE_SERVICE_SDK`。
3. 重新配置/构建，包含 SDK 的公开头文件，在业务 action 中调用现有 API。
   需要通知时，沿用 `lobbysvr/service/CMakeLists.txt` 中的通知声明，并在 main 注册生成 handler。
4. 验证一次请求和所需的通知/回调。普通接入不需要修改组件内部的 CMake helper、生成器或模板。

这是对**已有声明的参数补充**，不要重复定义服务目标。

## 快速上手：增加可复用组件

1. 纯算法组件参照 `src/component/GameSharedComponent/` 的 SDK 目录；
   “协议 + 服务 + SDK”组件参照 `src/component/dtmq/` 的结构。
2. 复制相应声明，替换组件名、协议目标名、protobuf service 全名、输出路径和导出宏。
   组件协议使用 `project_component_declare_protocol` 的 `PROTOCOLS` 清单；
   服务和 SDK 复用 `project_component_declare_service` / `project_component_declare_sdk`。
3. 在 `src/component/CMakeLists.txt` 登记目录。需要独立进程时，按[新增服务](add-service)登记
   类型与部署配置；纯算法 SDK 不需要进程配置。
4. 按[协议](add-protocol)和[RPC](add-rpc-task)上手填写声明，
   重新配置/构建后实现业务，消费方按前一节填写 SDK 依赖。

## 定制与详细设计

只有需要改变链接、生成布局或组件装配时才阅读
`src/component/component-functions.cmake` 与[RPC 与代码生成](../architecture/rpc-codegen)。
组件的复制、持久化和生命周期规则分别见其设计章节。
