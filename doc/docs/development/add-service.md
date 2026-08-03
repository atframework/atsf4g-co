---
title: 新增服务
---

# 新增服务

以 `echosvr` 为最小模板（仅一个 `app/echosvr_main.cpp`），标准服务参照 `lobbysvr`。

## 步骤

1. **定义协议**：在 `src/server_frame/protocol/`（或组件自己的 `protocol/`）添加 service 与 message；
   用 `atframework.service_options` / `rpc_options` 标注 `module_name` / `api_name` / `allow_no_wait`。
2. **声明生成规则**：在服务的 `CMakeLists.txt` 中用 `generate_for_pb_add_ss_service`（SS RPC）/
   `generate_for_pb_add_cs_service`（CS RPC）（见 `src/tools/generate_for_pb_utility.cmake`）声明：
   proto 文件 + 模板（`handle_ss_rpc` / `task_action_ss_rpc` / `rpc_call_api_for_ss` / CS 变体）+ 输出路径。
3. **编写 main**：参照 `src/lobbysvr/service/app/lobbysvr_main.cpp`：

```cpp
int main(int argc, char *argv[]) {
  atfw::atapp::app app;
  // logic_config 注入本服务配置加载回调
  project::logic_server_setup_common(app);
  // 注册生成的 RPC handler
  register_handles_for_<yourservice>();
  // 挂载 dispatcher 与业务 module
  app.add_module(atfw::cs_msg_dispatcher::me());
  app.add_module(atfw::ss_msg_dispatcher::me());
  app.add_module(atfw::db_msg_dispatcher::me());
  app.add_module(<业务 module>);
  return app.run(uv_default_loop(), argc, argv, nullptr);
}
```

4. **填充 task action**：生成器产出 `logic/action/task_action_*` 骨架，在 `operator()` 中写业务逻辑。
5. **加入构建**：`src/CMakeLists.txt` 中 `add_subdirectory(<name>svr)`。
6. **部署**：在 `install/cloud-native/charts/` 添加服务 chart（可复制现有服务），`values/` 各 profile
   补充参数；本地运行补充 `publish/tools/script/config.conf` 模板。

## 注意

- 生成物（`*.atfw.gen.*`）不入手工编辑；重新生成的触发是 proto/模板变化。
- 模块装配顺序遵循 `logic_server_setup_common` 的约定；自定义 module 依赖 dispatcher 时在其后挂载。
- 新服务默认进入全部构建，没有单独开关。
