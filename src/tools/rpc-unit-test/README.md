# rpc-unit-test

atsf4g-co 的 RPC 单元测试支持库：在普通进程内启动一个最小 atapp runtime，用内存 mock 引擎替代全部外部
依赖（SS/DNS/CS/DB/UUID/resource/HPA/telemetry），让业务代码的真实 RPC 路径（dispatcher、task、协程、
生成 API）可以在单测中被驱动和断言，无需 Redis、etcd、atbus 或系统 DNS。

## 适用范围与非目标

- 适用：service/component 逻辑的离线单元测试；router/SS/CS/DB 生成 API 的契约测试。
- 非目标：真实网络/存储的集成测试、性能测试、跨进程测试。
- 所有测试 executable 只使用 atframe_utils 私有测试框架（`CASE_TEST`/`CASE_EXPECT_*`），不引入 GTest。

## Hook 构建开关与生产开销

单元测试 seam 由 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` 整体门控（默认跟随
`BUILD_TESTING OR PROJECT_ENABLE_UNITTEST`）。hooks-off 的生产构建中：无测试状态、无热路径分支、无
`mock` 符号、无测试库依赖。所有 `mock` 子命名空间接口（生成 SS/DB mock、HPA 功能 mock）同样被宏
整体裁剪。

## 最小 CMake 示例

```cmake
project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-my-component-unit-test
  COMPONENT my-component
  SOURCES "my_test.cpp"
  LINK_LIBRARIES my-component-lib
  FEATURES SS DNS DB
  LABELS fast
  TIMEOUT 120)
```

`project_add_rpc_unit_test`（`src/tools/rpc-unit-test/cmake/ProjectRpcUnitTest.cmake`）参数：`TARGET`（必填）、
`COMPONENT`、`SOURCES`、`LINK_LIBRARIES`、`FEATURES`、`LABELS`（附加 CTest labels，惯例传 `fast`）、`TIMEOUT`
（秒，写入 CTest，默认 120）。它复用集中编译的 atframe_utils 私有 main/frame support targets，自动
`add_test`、配 labels、CTest timeout 和 Windows DLL `ENVIRONMENT_MODIFICATION` PATH。

## 最小 CASE_TEST 示例

```cpp
CASE_TEST(my_component, hello) {
  atframework::testing::runtime test;
  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss};
  CASE_EXPECT_EQ(0, test.start(options));
  if (!test.is_running()) {
    return;
  }

  auto task = test.run_task("hello", std::chrono::seconds{2},
                            [](rpc::context &) -> rpc::result_code_type { RPC_RETURN_CODE(0); });
  if (!task.empty()) {
    auto result = test.wait(task, std::chrono::seconds{5});
    CASE_EXPECT_TRUE(result.task_exited);
    CASE_EXPECT_EQ(0, result.result_code);
  }

  CASE_EXPECT_EQ(0, test.stop());
}
```

本文所有示例都由 `test/example_readme.cpp` 编译并执行，与真实 API 保持一致。

## 常用注册示例

### discovery 节点 + SS

```cpp
atframework::testing::mock_node node;
node.set_id(0x130001).set_name("remote").set_type_id(4097).set_type_name("remote-type").set_zone_id(1);
auto remote = test.discovery().add_node(node);
```

两个使用 discovery index（type index / 一致性哈希 / round-robin）的消费者侧注意点（rank/dtmq/
dtcoordsvr 等 SDK 都走这条路）：

- **注入后需要 reload**：`add_node` 直接写 atapp global discovery set，node 事件只在 etcd watch 路径
  触发；common module 的 discovery index 不会自动看到新节点。注入后调用一次
  `logic_server_last_common_module()->reload()`（回放现有节点进 index）。
- **HPA selector 标签过滤**：common module reload 时 HPA controller 会给 `scaling_ready` 的全部
  selector 打上 `hpa_scaling_ready=1` 标签要求；rank/dtmq/dtcoordsvr 等经
  `logic_hpa_discovery_select` 选节点的代码会按 metadata 过滤。mock 节点必须带同名标签：
  `node.add_label("hpa_scaling_ready", "1")`。

```cpp
// 引擎层 typed 规则：完整 RPC 名 + 请求/响应类型名
auto rule = test.ss().mock(
    "rpc_unit_test.RpcUnitTestService/rpc_unit_test_user",
    rpc_unit_test::RpcUnitTestEchoReq::descriptor()->full_name(),
    rpc_unit_test::RpcUnitTestEchoRsp::descriptor()->full_name(),
    [](const atframework::testing::ss_request_view &request, google::protobuf::Message &response)
        -> rpc::result_code_type {
      const auto &req = static_cast<const rpc_unit_test::RpcUnitTestEchoReq &>(request.body);
      static_cast<rpc_unit_test::RpcUnitTestEchoRsp &>(response).set_echo("hello " + req.payload());
      RPC_RETURN_CODE(0);
    });

// 生成层等价快捷方式（<service>::mock，typed 首参为 rpc::context&）：
// auto rule = rpc::unit_test::mock::rpc_unit_test_user(
//     [](rpc::context &, const rpc_unit_test::RpcUnitTestEchoReq &req,
//        rpc_unit_test::RpcUnitTestEchoRsp &rsp) -> rpc::result_code_type { ... });

// 期望（stop 时校验）与历史
test.ss().expect("rpc_unit_test.RpcUnitTestService/rpc_unit_test_user").times(1).to_node(0x130001);
test.ss().calls("rpc_unit_test.RpcUnitTestService/rpc_unit_test_user");
```

SS handler 返回 `rpc::result_code_type`，可以是协程：`RPC_AWAIT_CODE_RESULT` 等待嵌套 RPC（context 经
`ss_request_view.context` / typed handler 首参传入）；纯同步 handler 直接 `RPC_RETURN_CODE(...)`。两种
协程构建模式（std-coroutine / legacy）写法相同。

规则选项（`ss_rule_options`）：`match_node_id`（按目标节点匹配）、`times`（FIFO 脚本）、
`delay_generations`、`no_response`（模拟丢响应触发超时）、`malformed_type_url`/`malformed_body`。

### Router

`test.router_manager()` + `router_test_object` 把 router object 同步寻址到 mock 节点；router RPC 走
`mock://` connector，不修改 Mako。见 `test/rpc_unit_test_router.cpp`。

### DNS

```cpp
auto rule = test.dns().mock_a("example.unit-test.local", "192.0.2.1");           // A 记录
auto multi = test.dns().mock("multi.unit-test.local", records);                  // 多记录/AAAA
auto err = test.dns().mock_error("broken.unit-test.local");                      // 错误
// 延迟/乱序/timeout/unmatched 见 test/rpc_unit_test_dns.cpp
```

测试过程不产生任何系统 DNS 请求（hook 在 `uv_getaddrinfo` 之前）。

### DB

默认即完整 presence-aware 内存 backend（field merge/CAS/KL/TTL，语义对齐 Redis/Lua），无需注册任何规则：

```cpp
test.db().register_message_type<PROJECT_NAMESPACE_ID::table_login_auth>();  // 每个用到的表类型注册一次
```

生成层 per-table typed handler（SS 风格，每个生成表接口一个注册函数）：

```cpp
auto rule = rpc::db::login_auth::mock::get_all(
    [](rpc::context &, const PROJECT_NAMESPACE_ID::table_login_auth &input,
       PROJECT_NAMESPACE_ID::table_login_auth &output, rpc::unit_test::db_mock_meta &meta)
        -> rpc::result_code_type {
      output.set_open_id(input.open_id());
      output.set_user_id(7);
      meta.version = 3;            // CAS 版本：写接口传入期望值、读出记录版本
      RPC_RETURN_CODE(0);
    });
```

handler 首参 `rpc::context &`（可等待嵌套 RPC），末参统一为可扩展 `rpc::unit_test::db_mock_meta &`。
未注册 handler 的接口回落内存 backend；batch 组合接口由逐 key handler 自然覆盖。引擎层另有
`test.db().mock_table(name).on(op, fn)/on_any(fn)` 通用回调与 raw entry 数据准备 API
（`set_raw_kv`/`get_raw_kv`/`append_raw_kl`/`set_raw_ttl`，用于 CAS 版本种子/精确 bytes），以及表级
`calls(table, op)`/`last_call(table)` 统计。

### UUID / resource / CS

- UUID：`generate_global_increase_id` / `generate_global_unique_id` 经 DB `inc_field` 流入 DB mock（
  `feature::db` 即可覆盖；号段静态缓存跨 case 延续，不要断言绝对 id 值）；`generate_standard_uuid*` /
  `generate_short_uuid` 是纯本地函数，不触网、无需 mock。`feature::uuid` 当前不做额外 runtime 装配，
  仅作为 feature 标签保留。
- resource：`test.resource()` 提供 path→bytes/version/version_error 内存 loader，`reload()` 驱动真实
  manager 完整 reload/index 流程。
- CS：`test.cs()` / `mock_client` 模拟 gateway 客户端（add/post/remove/set-router-rsp），上行走真实
  dispatch/action，下行 data/kick/set-router/broadcast 全部可捕获，无 atbus。

### atapp raw transport 规则与历史

`test.transport()` 提供 id/name/discovery-node/consistent-hash/random/round-robin/metadata/broadcast 全维
度规则与调用历史断言，见 `test/rpc_unit_test_transport.cpp`。

## 默认严格策略与覆盖优先级

- 需响应 RPC（unary/wait 变体）：未匹配规则默认**快速失败**并输出调用与可用规则诊断。
- 单向通知类（stream/no-wait/broadcast/wait-later）：默认**记录并成功**（结果丢弃）。
- 两类默认值都可用 per-rule 选项覆盖（如把 stream 显式设为 fail 做负向断言），`mock_error` 直接注册
  错误响应。
- DB 覆盖顺序：生成层 typed handler → 引擎层 per-table 回调 → 内存 backend；默认不允许真实 Redis
  passthrough。

## mock 子命名空间与代码分布（3.5）

- 工具库公共 API：`atframework::testing::*`（`src/tools/rpc-unit-test`），只含公共代码。
- 生成 mock：`<service>::mock`（`rpc_call_api_for_ss.*.mako`）、`<db 命名空间>::mock`
  （`db_rpc_redis*.mako`），宏门控纯新增，编译进 server_frame/服务自身生成 TU。
- 功能私有 mock：`logic_hpa::mock`（`src/server_frame/logic/hpa/mock/`）。UUID 的 DB 型入口无独立 mock
  命名空间，直接经 DB hook 覆盖（见上节）。

## 外部依赖离线矩阵与禁止配置项

runtime 生成的配置保证离线：禁止 etcd、bus.proxy、主机名 listen、OTLP gRPC/HTTP exporter、prometheus
push/pull exporter、固定端口等（配置即 fail-fast）。telemetry 默认 noop 零网络，可选 otlp_file 导出
（`feature::telemetry`）；HPA 默认零网络，启用时自动安装 prometheus pull hook，预置指标经公开回调链
注入，未配置答案报错（`feature::hpa`）。注意 telemetry/冲突配置用例必须独占 executable（进程生命周期
状态），server_frame 组件测试因此拆成三个 target。

## Timeout 四层关系

RPC timeout（单次等待）< task timeout（`run_task` 参数）< runtime hard timeout（`wait` 参数）< CTest
timeout（`project_add_rpc_unit_test` 的 `TIMEOUT`）。hard timeout 会 poison 当前 runtime 并 kill-all
任务，此后该 runtime 只能 `stop()`，不能复用。

## fixture 生命周期

- 每个 `CASE_TEST` 独立 `runtime`：`start()` 建隔离 app/task/引擎，`stop()` 校验期望并按 deadline
  teardown；`stop()` 非零即失败。
- 单进程内 fixture 串行执行；连续 fixture 不得残留 app/task/session/静态状态（引擎规则 RAII + unbind
  统一清理）。
- Windows 运行测试可执行文件需要 DLL/PATH 配置（helper 已写入 CTest `ENVIRONMENT_MODIFICATION`，见
  `.agents/skills/testing/SKILL.md`）。

## 常见失败诊断

- 未匹配 RPC：快速失败，日志列出调用名与已注册规则；检查 RPC 全名与 `match_node_id`。
- response sequence 错误/不唤醒：确认 handler 返回 0 且响应类型与注册一致；stream/no-wait 不回响应。
- task 未进入 wait：`run_task` 返回空 handle 时打印 `task.get_diagnostic()`。
- 残留 session/task：`stop()` 诊断会列出；检查是否在 task 内遗漏 `RPC_RETURN_CODE`。
- `mock` 注册返回空 handle：`get_diagnostic()`（未绑引擎、类型名错误、RPC 名格式错误）。

## 组件私有 transport adapter / Orbit client adapter

组件私有直连 transport（如 Orbit 直连 atbus）应以独立可选 adapter target 接入：component-owned seam +
callback waiter，覆盖 callback/retry/error/timeout/fallback，外层调用仍在真实 `rpc::async_invoke` task
中（计划中，见 IMPLEMENTATION_PLAN.md 阶段 8）。

## 构建与运行

```powershell
# 构建自测
cmake --build build_jobs_cmake_tools --target atf4g-co-rpc-unit-test-selftest --parallel 12
# 按 label 运行
ctest --test-dir build_jobs_cmake_tools -L rpc-unit-test --output-on-failure
ctest --test-dir build_jobs_cmake_tools -L "component:server-frame" --output-on-failure
# 过滤单 case（atframe_utils 私有框架）
build_jobs_cmake_tools/publish/bin/atf4g-co-rpc-unit-test-selftest.exe -r "rpc_unit_test.combined_dns_ss_db_smoke"
```
