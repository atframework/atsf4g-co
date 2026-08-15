# rpc-unit-test

atsf4g-co 的 RPC 单元测试支持库：在普通进程内启动一个最小 atapp runtime，用内存 mock 引擎替代全部外部
依赖（SS/DNS/CS/DB/UUID/resource/HPA/telemetry），让业务代码的真实 RPC 路径（dispatcher、task、协程、
生成 API）可以在单测中被驱动和断言，无需 Redis、etcd、atbus 或系统 DNS。

> AI agent 使用本库时见 `.agents/skills/rpc-unit-test/SKILL.md`（流程化指引 + gotchas）；修改 mock 引擎/hook
> seam 时见 `.agents/skills/rpc-unit-test/references/engine-invariants.md`。

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
  CATEGORY component          # component（默认）| sdk | service
  SOURCES "my_test.cpp"
  LINK_LIBRARIES my-component-lib
  FEATURES SS DNS DB
  LABELS fast
  TIMEOUT 120)
```

`project_add_rpc_unit_test`（`src/tools/rpc-unit-test/cmake/ProjectRpcUnitTest.cmake`）参数：`TARGET`（必填）、
`COMPONENT`（必填，名称）、`CATEGORY`（可选，默认 `component`；控制 CTest label 前缀：`component` 适用于
component-functions.cmake 创建的目标，`sdk` 适用于库/SDK 测试，`service` 适用于服务内部测试）、`SOURCES`、
`LINK_LIBRARIES`、`FEATURES`、`LABELS`（附加 CTest labels，惯例传 `fast`）、`TIMEOUT`
（秒，写入 CTest，默认 120）。它复用集中编译的 atframe_utils 私有 main/frame support targets，自动
`add_test`、配 labels（`${CATEGORY}:<name>` 前缀）、CTest timeout 和 Windows DLL `ENVIRONMENT_MODIFICATION` PATH。

## 最小 CASE_TEST 示例

```cpp
CASE_TEST(my_component, hello) {
  atfw::testing::runtime test;
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss};
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

## 直接驱动服务端 SS action

测试服务内部的 `task_action_ss_rpc_base` 实现时，使用 `<atframework/testing/ss_action.h>` 的
`invoke_ss_action<TAction>`，不要在各测试里重复拼装 `SSMsg` 和调用 `task_manager`。它会走真实的 SSMsg
序列化/反序列化以及 action 创建、启动、等待流程：

```cpp
atfw::testing::ss_action_invoke_options invoke_options{
    rpc::my_service::packer::get_full_name_of_my_method()};
invoke_options.source.node_id = source_node_id;
invoke_options.source.node_name = "my-test-source";
invoke_options.source.source_task_id = source_task_id;
invoke_options.source.sequence = source_sequence;

auto task = test.run_task(
    "my_inbound_action", std::chrono::seconds{2},
    [request, invoke_options](rpc::context &ctx) -> rpc::result_code_type {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
          atfw::testing::invoke_ss_action<task_action_my_method>(ctx, request, invoke_options)));
    });
```

使用约束：

- runtime 必须启用 `feature::ss`；只在 `run_task` 或另一个真实 action 协程内用 `RPC_AWAIT_CODE_RESULT`
  等待它。接口只返回 action 的最终 task result，不等同于响应 protobuf 中的业务码。
- `TAction` 必须派生自 `task_action_ss_req_base`。请求参数直接绑定为 `TAction::rpc_request_type`，错误请求类型
  会编译失败；RPC 名还会在创建 action 前通过 protobuf method descriptor 同时校验请求和响应类型。
- RPC 名只传生成的 `packer::get_full_name_of_<rpc>()`，不要手写字符串。手写值只用于故意构造非法 RPC 名的
  负向测试。
- 请求与选项按值快照进协程帧，传临时对象也不会悬空；相应地，调用后的外部修改不会影响本次 action。
- `source` 的零 ID 表示匿名/系统来源。测试 `forward_rpc`、链路追踪、回包或来源鉴权时，必须显式填写所有
  相关来源字段，避免默认值掩盖行为。
- 该接口按模板参数直接创建已知 action，**不测试 dispatcher 的 RPC→action 注册查找**。注册表、未知 RPC、
  非法 type URL/body/envelope 等测试应使用 raw transport/dispatcher 接口。

可编译的最小用法见 `test/example_readme.cpp`，接口契约自测见 `test/rpc_unit_test_ss_action.cpp`；包含多种
action 和 `forward_rpc` 来源透传的实际案例见 `src/component/dtmq/test/dtmq_test_task_forward.cpp`。

## 常用注册示例

### discovery 节点 + SS

```cpp
atfw::testing::mock_node node;
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
// 引擎层 typed 规则：完整 RPC 名 + 请求/响应类型名。RPC 全名用生成接口
// rpc::<module>::packer::get_full_name_of_<rpc>()（gsl::string_view），不要硬编码字符串：
auto rule = test.ss().mock(
    rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user(),
    rpc_unit_test::RpcUnitTestEchoReq::descriptor()->full_name(),
    rpc_unit_test::RpcUnitTestEchoRsp::descriptor()->full_name(),
    [](const atfw::testing::ss_request_view &request, google::protobuf::Message &response)
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
test.ss().expect(rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user()).times(1).to_node(0x130001);
test.ss().calls(rpc::unit_test::packer::get_full_name_of_rpc_unit_test_user());
```

> SS/CS 模板为每个 RPC 在 `packer` 子命名空间中生成全名接口（与 `pack_<rpc>`/`unpack_<rpc>` 一致），例如
> `rpc/router/routerservice.atfw.gen.h` 的 `rpc::router::packer::get_full_name_of_router_transfer()`，
> CS 模板同理（如 `rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync()`）。向 `test.ss()`
> 注册/断言 mock 时优先使用它，避免 RPC 重命名或包名变更时字符串漂移。orbit fork 模板同样在 `packer` 子命名空间中
> 生成同名接口，但其返回值为 orbit 协议的点分全名（如 `hello.OrbitClientRpcService.echo`），而 `test.ss()` 校验
> 要求 `/` 分隔符（`mock_ss.cpp`）；orbit RPC 经 orbit transport 不走 SS 引擎，因此 orbit fork 的 getter 不可传入
> `test.ss()`。

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

写路径的权威契约是 `src/server_frame/dispatcher/db_msg_dispatcher.cpp` 内嵌的 Lua 脚本；
`src/server_frame/test/server_frame_test_db_script_contract.cpp` 以行为用例固化该契约（CAS 读写版本一致、期望版本
0 = 忽略版本比较强制覆盖且新版本仍按真实版本 +1；insert 仅接受无版本记录，冲突返回 `EN_DB_KEY_EXISTS`；
所有写路径按 HSET 语义只合并 present 字段；KL 索引 1 起单调不复用，达到 `max_list_length` 时按最小索引淘汰
一条，`max_list_length` 0 仅保留一条）。修改脚本时必须同步更新 mock_db 与这些用例；引擎级等价语义由
`test/rpc_unit_test_db.cpp` 固化。

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

- 工具库公共 API：`atfw::testing::*`（`src/tools/rpc-unit-test`），只含公共代码。
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

组件私有直连 transport（如 Orbit 直连 atbus）以独立可选 adapter target 接入：component-owned seam +
callback waiter，覆盖 callback/retry/error/timeout/fallback，外层调用仍在真实 `rpc::async_invoke` task
中。Orbit 客户端 SDK 已按此模式实现并自测（见 `src/component/GameSharedComponent/Orbit/test/`），不由本库
runtime 引擎驱动。

## 构建与运行

```powershell
# 构建自测
cmake --build build_jobs_cmake_tools --target atf4g-co-rpc-unit-test-selftest --parallel 12
# 按 label 运行
ctest --test-dir build_jobs_cmake_tools -L rpc-unit-test --output-on-failure
ctest --test-dir build_jobs_cmake_tools -L "sdk:server-frame" --output-on-failure
# 过滤单 case（atframe_utils 私有框架）
build_jobs_cmake_tools/publish/bin/atf4g-co-rpc-unit-test-selftest.exe -r "rpc_unit_test.combined_dns_ss_db_smoke"
```
