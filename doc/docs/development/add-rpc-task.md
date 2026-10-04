---
title: 新增 RPC 与 task action
---

# 新增 RPC 与 task action

## 快速上手：已有服务增加 SS RPC

以大厅服为例，修改 `src/lobbysvr/protocol/protocol/pbdesc/lobby_service.proto`。
沿用文件已有的 package 和 import，增加请求/响应，并将方法加入已有 `LobbysvrService`：

```protobuf
message SSEchoTextReq { string text = 1; }
message SSEchoTextRsp { string text = 1; }

// Add inside the existing LobbysvrService.
rpc echo_text(SSEchoTextReq) returns (SSEchoTextRsp) {
  option (atframework.rpc_options) = {
    module_name: "action"
    api_name: "EchoText"
    allow_no_wait: false
  };
}
```

按[统一构建步骤](overview)重新配置并构建，不用新增模板或修改现有生成流程。
生成器创建 `src/lobbysvr/service/logic/action/task_action_echo_text.h/.cpp`，并更新注册代码和 SDK。
在新 action 的 `operator()` 中写入：

```cpp
task_action_echo_text::result_type task_action_echo_text::operator()() {
  get_response_body().set_text(get_request_body().text());
  RPC_RETURN_CODE(0);
}
```

再次构建后，服务端通过已有的 `register_handles_for_lobbysvrservice()` 注册新方法。
调用方包含生成的 `rpc/lobby/lobbysvrservice.atfw.gen.h`，使用其中的 `echo_text` 接口；
目标参数和命名空间以生成的函数声明为准。

## 等待 RPC 完成 {#await-rpc}

调用需要回包的 RPC 时，使用以下宏等待 RPC 完成后再处理回包。
即使不关注回包或返回值，也要用 `RPC_AWAIT_IGNORE_RESULT` 显式标明，不直接丢弃调用返回的框架结果对象。

| 用法 | 作用 |
| --- | --- |
| `RPC_AWAIT_CODE_RESULT(call)` | 等待调用完成并取得整数错误码 |
| `RPC_AWAIT_TYPE_RESULT(call)` | 等待调用完成并取得对应类型的结果值 |
| `RPC_AWAIT_IGNORE_RESULT(call)` | 等待调用完成，显式忽略返回结果 |

这些宏统一适配 C++20 协程和传统有栈协程，业务代码使用同一套等待方式。
忽略结果仍会等待 RPC 完成；需要只发送不等回包时，使用声明允许的生成 no-wait 接口。

## 快速上手：已有服务增加 CS RPC

1. 在 `src/server_frame/protocol/public/protocol/pbdesc/` 定义客户端消息，参照已有
   `com.protocol.*.proto`，并由 `com.protocol.proto` import。
2. 在 `com.protocol.proto` 的已有 `LobbysvrClientService` 或 `AuthsvrClientService` 中添加方法，
   按同类 RPC 填写 `module_name`、`api_name` 和请求/响应类型。
3. 重新配置并构建，在新 `task_action_*` 中实现业务，再构建并同步客户端协议。
   已有服务的生成配置和 handler 注册无需重复添加。

`task_action_cs_req_base` 提供 session 与响应处理。已有服务配置启用了
`RPC_IGNORE_EMPTY_REQUEST`，新增上行请求应定义实际请求 message，并参照现有方法的方向约定；
流式下行通常用 `google.protobuf.Empty` 请求。

## 修改已有 RPC

已有业务骨架默认不覆盖；自动生成的 handler 和调用接口会更新。
改变请求/响应类型、方法名或 `module_name` 时，检查原 action 的基类、头文件、目录和调用点，
手工迁移已有业务逻辑。删除 RPC 后也要清理不再使用的业务 action。
`*.atfw.gen.*` 不应手改。

## 常用声明选项

| 声明 | 用途 |
| --- | --- |
| `rpc_options.module_name` | action 所在的业务子目录 |
| `service_options.module_name` | SDK 输出目录与命名空间组织 |
| `api_name` | RPC 的 API 标识；生成的 C++ 函数名仍按 rpc 方法名生成 |
| `allow_no_wait: true` | 允许生成免等待调用方式；以生成的具体接口为准 |
| `returns (stream X)` | 服务端流式下行，例如 `user_dirty_chg_sync` |
| `rpc x(stream Req)` | 请求流，例如 `channel_event_sync` |

## 无消息任务

不经 RPC 触发的定时任务可参照 `src/server_frame/router/action/task_action_auto_save_objects`。
生成骨架的辅助入口为 `src/generate-nomsg-task.sh`；业务仍需安排启动或定时触发。

## 定制与详细设计

新 protobuf service 的一次性接入见[新增服务](add-service)；
需要改生成布局、过滤规则或模板时阅读[RPC 与代码生成](../architecture/rpc-codegen)，
修改调度行为时阅读[任务与分发](../architecture/task-dispatcher)。
验证真实调用路径可使用[离线 RPC 单元测试](rpc-unit-test)。
