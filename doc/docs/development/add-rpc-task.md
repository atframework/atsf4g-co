---
title: 新增 RPC 与 task action
---

# 新增 RPC 与 task action

## SS RPC（服务间）

1. 在 proto 中给已有 service（或新 service）添加方法：

```protobuf
service MyService {
  option (atframework.service_options) = {module_name: "my_module"};
  rpc my_method(MyMethodReq) returns (MyMethodRsp) {
    option (atframework.rpc_options) = {api_name: "my_method" allow_no_wait: false};
  }
}
```

2. 重新构建：protoc + mako-generator 自动产出：
   - 服务端 task action 骨架 `task_action_my_method.{h,cpp}`（已存在的手写部分保留在标记区间内）；
   - handler 注册 `handle_ss_rpc_<service>.atfw.gen.*` 更新；
   - 调用端 API `rpc_call_api`（`<service>.atfw.gen.*`）。
3. 在骨架的 `operator()` 中实现业务：

```cpp
task_action_my_method::result_type task_action_my_method::operator()() {
  MyMethodRsp &rsp = get_response_body();
  // ... 业务逻辑，可 RPC_AWAIT_CODE_RESULT(rpc::db::xxx(...))
  RPC_RETURN_CODE(0);
}
```

4. 调用方：

```cpp
MyMethodReq req;
// ... 填充请求
auto res = RPC_AWAIT_CODE_RESULT(rpc::MyService::my_method(ctx, req, /*目标*/...));
```

## CS RPC（客户端）

在 `com.protocol*.proto`（public）中定义消息与 service，CMake 声明 `handle_cs_rpc` /
`task_action_cs_rpc` 模板；生成的 action 基类是 `task_action_cs_req_base`，自带 session 校验与下行打包。
下行推送 API 由 `session_downstream_api_for_cs.*.mako` 生成。

## 无消息任务

定时/自驱动任务使用 `task_action_no_msg.*.mako` 模板，或用
`src/generate-nomsg-task.sh`（`.in`）快速生成骨架；框架内示例：`task_action_auto_save_objects`。

## 常用选项

| rpc_options | 效果 |
| --- | --- |
| `allow_no_wait: true` | 调用端只发不等响应（无 `co_await` 结果） |
| stream 返回（`returns (stream X)`） | 服务端流式下行推送（如 `com.protocol.proto` 的 `player_dirty_chg_sync`） |
| stream 请求（`rpc x(stream Req) returns (...)`） | 调用端流式上行/免等待响应（如 dtmq 的 `channel_event_sync`） |
| `api_name` | 生成的调用端函数名 |
