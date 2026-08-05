# RPC 与资源 Hook 单元测试工具执行计划

> 状态：已源码复核（含 telemetry/HPA/router/DNS/CS/DB/UUID/Orbit 逐条验证），待实施  
> 最后复核：2026-08-03  
> 计划目录：`src/tools/rpc-unit-test`  
> 本文件只描述实施方案；当前阶段不新增或修改任何 C++、Mako、CMake 实现。

## 1. 目标与边界

在 `src/tools/rpc-unit-test` 建立一个仅用于本工程单元测试的支持库，使组件可以在不启动真实服务、Redis、DNS、
atbus 或网关进程的条件下，运行真实的 RPC 生成代码、dispatcher、task 和配置加载流程。

必须满足以下目标：

- 测试入口严格使用 `atframework/atframe_utils/test` 的私有测试框架和 `CASE_TEST`，不链接或探测 GTest。
- 每个组件用一个 CMake 函数创建独立测试可执行文件，同时自动注册 CTest、标签、超时和运行时依赖。
- 测试代码通过一个进程级 fixture 完成 atapp、公共模块、服务发现、mock 注册、task 启动、事件泵和清理。
- SS、DNS、CS、DB RPC 都在真实 task 上下文中调用；调用方不需要区分 C++20 协程和传统协程。
- 默认使用内存 DB 和确定性 UUID；二者均可整体替换或按操作覆盖。
- 支持内存资源提供者并通过真实配置 manager 的 reload/index 流程加载资源。
- 网络类 mock 分两类默认行为：单向通知类 RPC 未配置时记录并正常返回（丢弃结果），需要 response 的 RPC 未配置
  时立即返回错误码并输出诊断，避免测试静默等待到超时（见 3.4）。
- 单个测试进程不访问真实 Redis、DNS、atbus 或外部服务，除非测试显式选择 passthrough。

第一版不把以下内容伪装成已覆盖：

- 组件私有的非 SS 传输不冒充基础 SS 覆盖；首版为当前唯一的生产直连实例
  `GameSharedComponent/Orbit` 客户端 SDK 提供独立 Orbit adapter target。示例 `echosvr` 的 atbus demo 不纳入 RPC
  工具范围。
- HTTP/libcurl 等外部请求（HPA controller、etcd 工具、telemetry OTLP/Prometheus push exporter 分别使用
  `util::network::http_request`（存量老名，等价于 `atfw::util::network::http_request`）、libatapp etcd HTTP API
  和 opentelemetry/prometheus-cpp 内置 client）；它们不是本工程内置消息 RPC，由组件自行注册资源/网络适配器。
- 多个 fixture 在同一进程并发运行。现有 app、dispatcher、task manager 和配置 manager 含进程级单例，首版采用
  “一个测试可执行文件内一次只运行一个 runtime”的约束；CTest 进程间仍可并行。

## 2. 已核实的工程事实

以下结论来自当前仓库源码，不依赖假设。

### 2.1 测试与构建

- 私有测试入口位于 `atframework/atframe_utils/test/app/main.cpp`；case 注册、断言与生命周期位于
  `atframework/atframe_utils/test/frame`（`test_macros.h`、`test_case_base.cpp`、`test_manager.cpp`）。
  `CASE_TEST` 在 `test_macros.h` 通过静态对象构造期调用 `test_manager::me().append_test_case` 自动注册；
  CLI 支持 `-l/--list-tests`、`-f/--filter`、`-r/--run`；无框架宏时 `main` 直接走 `run_tests(argc, argv)`。
- `atframe_add_test_executable`（`atframework/atframe_utils/test/test.build_bin.cmake`）在
  `if(TARGET GTest::gtest OR GTest::GTest)` 时追加 `PROJECT_TEST_MACRO_ENABLE_GTEST=1` 并链接 GTest。注意
  一个额外事实：私有框架源码实际判断的宏是 `UTILS_TEST_MACRO_TEST_ENABLE_GTEST`，与该 cmake 定义的宏名不一致，
  即当前 vendored 副本中该切换路径处于惰性状态。本工具仍不复用该 helper：避免 vendored 升级后行为漂移，且该
  helper 本身也不负责 `add_test`。本工具不链接任何 GTest target、不定义任何框架切换宏；私有 main 与 frame
  实现分别集中编译成测试专用 OBJECT/STATIC support target，再由各测试 executable 复用，避免每个组件重复编译
  同一份框架实现。
- `src/component/test/ItemAlgorithmTest/CMakeLists.txt` 已展示私有框架的手工链接方式（直接编译 frame 源码 +
  `target_include_directories` 指向 frame 目录），但它**不调用 `add_test`**，且由
  `src/component/test/CMakeLists.txt` **无条件**引入（不受 `PROJECT_ENABLE_UNITTEST` 门控）。本工具要补上统一的
  CTest 注册、标签和超时管理。
- 根 `CMakeLists.txt` 仅在 `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` 时 `include(CTest)`；
  `PROJECT_ENABLE_UNITTEST`（`project/cmake/ProjectBuildOption.cmake`）默认 `OFF`。因此本工具的
  `add_subdirectory` 和 helper 注册必须同样以 `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` 为门控。
- `src/CMakeLists.txt` 的 `add_subdirectory` 顺序为 `server_frame` → `component` → `echosvr` → `cachesvr` →
  `authsvr` → `lobbysvr` → `rank_settlement_svr` → `orbitsvr` → `tools` → `robot`。CMake 的
  `function`/`macro` 不会被提升（调用前必须已定义），因此若组件要在自己的 `CMakeLists.txt` 调用本工具的函数，
  必须把 `tools/rpc-unit-test` 的 CMake 定义提前到 `component` 之前解析；target 的链接引用可后置，函数定义不行。
- 根工程名为 `atf4g-co`；server frame 链接变量是 `PROJECT_SERVER_FRAME_LIB_LINK`（值为 `server_frame`，见
  `src/server_frame/server_frame.cmake`），安装导出命名空间也是 `${PROJECT_NAME}::`。**所有 CMake target 名称
  禁止硬编码工程名前缀**：target 声明、alias、`if(TARGET ...)`、依赖、folder、label 与诊断字符串一律写
  `${PROJECT_NAME}-<name>`/`${PROJECT_NAME}::<name>`（或组件既有 `atframework` 前缀），不写字面量
  `atf4g-co-...`。本工具据此固定使用 CMake 函数 `project_add_rpc_unit_test`、真实 target
  `${PROJECT_NAME}-rpc-unit-test` 和 alias `${PROJECT_NAME}::rpc-unit-test`；C++ API 命名空间独立采用
  `atframework::testing`。
- 构建开关 `PROJECT_ENABLE_UNITTEST` 默认 OFF；`PROJECT_SERVER_FRAME_USE_STD_COROUTINE`
  （`project/cmake/ProjectBuildOption.cmake`）是 `cmake_dependent_option`：编译器通过
  `COMPILER_OPTIONS_TEST_STD_COROUTINE` 检测时默认 ON，否则强制 OFF；验收应分别以 `ON` 和 `OFF` 构建。
- 当前工作区（`.vscode/settings.json`）使用 Ninja、Debug、C++20、`PROJECT_ENABLE_UNITTEST=ON`，
  `compile-commands-dir` 指向 `build_jobs_cmake_tools`（解析出的 `<BUILD_DIR>`）。
- Windows 上运行测试可执行文件的 DLL 依赖没有统一拷贝机制：现状是统一 bin 输出目录、既有
  `project_setup_runtime_post_build_bash/pwsh` 钩子和 PATH 注入。CTest 可用 CMake 3.24 已支持的
  `ENVIRONMENT_MODIFICATION` 追加 PATH；直接运行 executable 时仍按 `.agents/skills/testing/SKILL.md` 设置环境。

### 2.2 服务发现与 atapp 传输

- `logic_server_common_module` 从所属 app 获取 `service_discovery_module`，并基于 app 的 global discovery 建立
  type/zone/name 索引（`logic_server_setup.*`）。
- `logic_server_setup_common` 按 `logic.server.shared_component` 配置初始化 task/session/router/excel 等共享组件；
  runtime 不能只在 C++ 中 add module 而不生成匹配配置。
- 注入 discovery 节点的稳定入口是 `app.get_service_discovery_module()->get_global_discovery()`，它返回
  **非 const** 的 `etcd_discovery_set&`（`libatapp/include/atframe/modules/service_discovery_module.h`；
  `app` 自身只有 const 重载，必须经 module 获取），可调用 `add_node(const etcd_discovery_node::ptr_t&)` /
  `remove_node(...)`；节点经 `atfw::util::memory::make_strong_rc<etcd_discovery_node>()` 构造后用
  `etcd_discovery_node::copy_from(atapp_discovery, node_version, 0)` 填充。libatapp 大量单元测试
  （`atframework/libatapp/test/case/atapp_message_test.cpp`、`atapp_upstream_forward_test.cpp` 等）正是用此
  入口注入 fake 节点，是已存在的扩展面。
- `atapp::app::add_connector<TCONNECTOR>(args...)` 是公开模板（`atapp.h`），内部
  `make_shared<TCONNECTOR>(*this, args...)` 后经私有 `add_connector_inner` 注册。connector 基类
  `atapp_connector_impl`（`include/atframe/connectors/atapp_connector_impl.h`）要点：
  - `register_protocol(const std::string&)` 为 **protected**，在子类构造中调用以声明可处理的 ingress scheme；
    endpoint 依据 discovery 节点 ingress 地址的 scheme 选择 connector；
  - `get_address_type(const channel_address_t&)` 是**纯虚**，mock connector 必须实现；
  - 建连/断连回调为 `on_start_listen` / `on_start_connect(discovery, endpoint, addr, handle)` /
    `on_close_connection(handle)`；mock 需要在 `on_start_connect` 中给出可用的 fake handle；
  - 仓库中可参照的实现仅有内置 `atapp_connector_loopback`（`register_protocol("loopback")`）和
    `atapp_connector_atbus`；libatapp 与本仓库 `src/` 下目前**没有**自定义 connector 的先例，mock connector
    属于首个，实现时以 loopback connector 为最近参照。
- 出站方向：`ss_msg_dispatcher::send_to_proc` → `app::send_message(node_id, kInServerMessage, …)` → endpoint →
  `connector->on_send_forward_request(handle, type, uint64_t* msg_sequence, data, metadata)`。mock connector 在
  此捕获出站字节，不在发送调用栈内同步回包。
- 入站方向有两条独立通道，必须区分：
  - **投递结果 ack**：connector 的 `on_receive_forward_response(direct_source_id, handle, type, msg_sequence,
    error_code, data, metadata)`，或 app 的 `trigger_event_on_forward_response(sender, msg, error_code)`，
    告知发送侧“已投递/失败”，不携带 RPC 回包。
  - **回包 / 远端主动消息**：mock 通过 `app::trigger_event_on_forward_request(sender, msg)` 注入一条入站
    `message_t`，它触发 server frame 注册的 `app_default_handle_on_receive_request`（`logic_server_setup.cpp` 中
    `set_evt_on_forward_request`），进入真实 `ss_msg_dispatcher`，按 `destination_task_id` + sequence 唤醒原
    task。SS 回包正是走这条路径。
  - 相关结构：`message_t{int32_t type; uint64_t message_sequence; span<const unsigned char> data; const
    atapp_metadata* metadata}`、`message_sender_t{app_id_t direct_source_id; app_id_t id; string_view name;
    atapp_endpoint* remote}`（`atapp.h`）。
- SS dispatcher 的 node-id、node-name、discovery-node 发送最终都经过 `app::send_message`；broadcast 遍历真实
  discovery 索引后仍逐个走相同发送路径。
- atapp 还公开按 consistent-hash、random、round-robin 和 discovery-set 发送的重载；它们最终同样进入
  endpoint/connector。本工具必须把 connector 暴露为独立 raw transport mock，而不是只作为 SS rule engine 的
  私有实现，以验证 atapp 层服务发现选择和数据发送接口。
- atapp 初始化会创建 bus node，再遍历配置中的 listen 列表；空列表不会绑定真实 endpoint。因此默认配置应使用
  “bus node 存在但 listen 为空”，并用工具 smoke test 固化这一事实，而不是使用需要真实内存地址的 `mem://` 或
  固定 TCP 端口。

### 2.3 task、等待与事件泵

- `rpc::async_invoke`（`src/server_frame/rpc/rpc_async_invoke.h`）会创建并启动真实 task，可传入 task 超时，
  返回统一的 `rpc::async_invoke_result`。
- `task_type_trait`（`src/server_frame/dispatcher/task_type_traits.h`，std-coroutine 与 libcopp 两套实现）已统一
  标准协程 future 和传统 task pointer，均可查询 empty、exiting、timeout、cancel、fault、result 和 task id。
- `rpc::wait`（`rpc_utils.h`）、`rpc::wait_task(s)`（`rpc_async_invoke.h`）与 `RPC_AWAIT_*`/`RPC_RETURN_*` 宏
  （`rpc/rpc_common_types.h`，按协程模式分两套定义）已经封装两种协程模式，测试业务代码不应再分叉。
- 初始化后的 app 可通过 `run_noblock(max_event_count = 20000)` 推进模块 tick、dispatcher 和 task；返回 0 表示无
  更多事件、1 表示仍有 pending、负数表示错误，泵循环可据此决定 yield。注意 `app::tick()` 实际会跑
  `mod->tick()`、`bus_node_->proc()` 和内部/自定义定时器，但**不调用 `uv_run`**，因而不驱动共享
  `uv_default_loop` 上的 socket IO 轮询（所以 libatapp 部分单测只用 `tick()`）；真正的 IO 投递需
  `run_noblock`/`run_once`（内部 `run_inner` 调 `uv_run`）。
- libatapp 单测的驱动范式同时包含 `atfw::util::time::time_utility::update()` 刷新缓存时钟；task 超时、TTL 等
  依赖缓存时钟，本工具的事件泵必须包含这一步。
- `task_manager` 仅公开 `kill_all`，`task_type_trait` 只提供状态查询/复位，没有安全的单 task kill。因此 hard
  timeout 后不能声称只回收目标 task；必须把当前 runtime 标记为 poisoned，终止其全部 task 并进入 teardown。
- app 的正常 `stop()` 只触发停止流程，析构仍可能按配置的 stop timeout 等待；默认无效值可能带来较长等待。
  测试配置必须设置短且非零的 stop timeout/interval，并在释放 app 前 pump 到 `is_closed()`，另设 teardown
  wall-clock deadline。

### 2.4 SS RPC

- 普通生成接口在 `src/templates/rpc_call_api_for_ss.cpp.mako` 中组装 `SSMsg`，通过
  `ss_msg_dispatcher::send_to_proc` 发送（最终 `owner->send_message(node_id, kInServerMessage, …)`，见
  `ss_msg_dispatcher.cpp`），并按 task id 与 sequence 等待响应（`internal::wait_and_unpack_ss_response`）。
- 已有生成路径包含 unary、stream、no-wait/wait-later、id/name/discovery-node、metadata、broadcast、user RPC 和
  router RPC。
- 普通 SS、stream 和 broadcast 都汇聚到 dispatcher/app 传输，不需要在每个生成 API 中增加 mock 分支。
- router RPC 的准确路径（已逐行核实）：模板先 `router_manager_set::me()->get_manager(type_id)` 取 manager，调用
  `router_manager_base::send_msg` → `send_msg_raw`（填充 `SSRouterHead`，按 router 缓存解析
  `target_server_id`），最终仍调用 `ss_msg_dispatcher::me()->send_to_proc(target_server_id, msg)`。即 **router
  RPC 只在寻址层绕过 dispatcher 的 node 查找，物理传输与回包等待链和普通 SS 完全一致**，mock connector 同样能
  捕获其出站字节。真正的前置条件是 fixture 内必须存在可用的 router manager：
  `router_manager_set::register_manager/unregister_manager` 是公开 API，且 `router_manager_base` 构造/析构会
  自动注册/注销。但 manager 的带 `io_task_guard&` 的 `mutable_cache` 必须返回一个可用的 `router_object_base`；
  后者还有 `name`/`pull_object`/`save_object`/`save(带 io_task_guard&)` 等纯虚接口，所以必须同时实现
  `router_test_manager` 和 `router_test_object`，不能只实例化 manager（完整纯虚清单见 8.2）。
- 当前 `svr.protocol.proto` 的 `${PROJECT_NAMESPACE}.RouterService/router_transfer` 是普通 unary SS 方法，没有
  `router_rpc: true`；当前仓库没有启用中的 `router_rpc: true` 协议实例，只有注释示例。它可用于内置 unary
  smoke，但不能证明 Mako 的 router 分支。测试专用 proto 因而是必需输入，用于稳定实例化 router/user/no-wait/
  wait-later 等当前内置协议不能完整覆盖的生成分支，生成物仍只落构建树。
- 正常响应必须使用请求的 source task id 和 sequence（回包时写入 `destination_task_id`），并携带相同 RPC 名称、
  响应 type URL、body 和错误码；只有这样才能由真实 dispatcher 唤醒原 task。

### 2.5 DNS RPC

- `rpc::dns::lookup(ctx, domain, output)`（`src/server_frame/rpc/dns/lookup.*`）用
  `TASK_COMPAT_CHECK_TASK_ACTION_RETURN` 要求必须在 task 中调用，`allocate_sequence()` 后调用
  `ss_msg_dispatcher::send_dns_lookup(domain, sequence, task_id)`，再用 `rpc::custom_wait` +
  `get_dns_lookup_rpc_type()`（即 `&running_dns_lookup_`）等待，默认超时取
  `logic_cfg().dns().lookup_timeout()`。
- `send_dns_lookup(gsl::string_view domain, uint64_t sequence, uint64_t task_id)` 直接调用 libuv `uv_getaddrinfo`
  （loop 取 `get_app()->get_bus_node()->get_evloop()`，无 app/bus node 时 `uv_default_loop()`），完成路径
  `dns_lookup_callback` 构造 `dispatcher_resume_data_type`（message type 为 `get_dns_lookup_rpc_type()`，msg_addr
  指向 `std::vector<address_record>`，即 `rpc::dns::details::callback_data_type`），携带原 sequence 调用
  `rpc::custom_resume`。调用方 `rpc::dns::lookup` 默认超时取 `logic_config::me()->get_logic_cfg().dns().lookup_timeout()`
  （缺省值 5 秒）。
- 因而 DNS 最小 hook 点应位于发起 `uv_getaddrinfo` 之前（`send_dns_lookup` 内部），最贴近“拦截真实网络调
  用”；另外 `dns_lookup_callback` 内构造 `records` + `custom_resume` 处是对称的“完成侧” seam（两处皆可注入伪
  造结果）。完成仍复用现有 custom-resume 路径，测试 hook 不直接设置 task 结果。

### 2.6 CS RPC

- 上行消息可直接调用 `cs_msg_dispatcher::dispatch(source, msg)` 注入（无需经 atbus），但 `msg` 必须满足真实线
  格式：`msg.type == 静态转型自 ::atfw::component::service_type::kAtGateway`，body 为 `atfw::gateway::server_message`，
  分发基于 `req_msg.body().cmd_case()`（枚举作用域是 `server_message_body::`）：`kPost` / `kAddSession` /
  `kRemoveSession` / `kSetRouterRsp`（`cs_msg_dispatcher.cpp`）。`kAddSession` 经 `session_manager::me()->create`
  创建 session；`kPost` unpack `CSMsg` 后经基类 `on_receive_message` 触发真实 action task（非直接 `create_task`）；
  `kRemoveSession` 启动登出 task。mock client 注入上行时必须按此封装。
- `src/templates/session_downstream_api_for_cs.cpp.mako` 生成的下行调用最终进入
  `session::send_msg_to_client` → `cs_msg_dispatcher::send_data`，广播进入 `broadcast_msg_to_client` →
  `cs_msg_dispatcher::broadcast_data`。
- CS 下行直接调 `owner->get_bus_node()->send_data(...)` 的调用点共四处：`send_kickoff`、`send_set_router`、
  `send_data`、以及多会话版 `broadcast_data(node_id, session_ids, …)`（注意其 `session_ids` 参数当前未被使用，
  两参数版 `broadcast_data` 只是转发到 `send_data`）。因此需先将这些分支收敛到 CS dispatcher 的**单一
  gateway-send 内部函数**再加窄 hook，才能同时覆盖 data、kickoff、set-router 和 broadcast；多会话版 broadcast
  的参数语义问题在本变更中只记录并保持 fallback，不顺带改变生产行为。

### 2.7 DB RPC 与 UUID

- 生成 DB API（`db_interface`/`db_rpc_redis*` 模板产物）负责构造规范 key 和 protobuf 类型，最后统一进入
  `src/server_frame/rpc/db/hash_table.*`。
- `hash_table` 公共操作清单（已核实）：KV：`get_all`、`partly_get`、`batch_get_all`、`batch_partly_get`、`set`
  （单一函数，CAS 经可选 `uint64_t *version` 参数，无独立 CAS 函数）、`inc_field`；KL：`get_all`、`get_by_indexs`、
  `update_by_index`、`add_index`、`remove_by_index`（两个重载）；通用：`remove_all`、`set_ttl`、`remove_ttl`。其中 `batch_get_all` /
  `batch_partly_get` 内部用 `rpc::async_invoke` + `rpc::wait_tasks` 并发调用 `get_all`/`partly_get`，因此
  只要 hook 单条原语，batch 自然被覆盖。
- 当前 `src/` 中所有直接 `db_msg_dispatcher::send_msg` 调用都位于 `hash_table.cpp`，没有第二条绕过该层的 DB
  wire 出口；因此覆盖完整 hash-table 公共原语即可接管当前生成 DB API。
- 各操作在发送前还有 task、descriptor、字段、空索引/message 等本地同步校验，之后才构造 Redis 命令并通过
  `db_msg_dispatcher::send_msg` 发送，最后以 `RPC_DB_RETURN_CODE`（即 `RPC_RETURN_TYPE`，`db_utils.h`）返回。
  **seam 位于全部本地校验之后、Redis 专用参数构造/首次发送之前**，比 Redis/hiredis wire 层更适合作为逻辑 mock
  边界。
- `hash_table` 接口返回 `result_type` 在两种协程模式下类型不同；为避免把协程返回类型泄漏到 hook ABI，hook
  应设计为**同步** `handled + result code` 接口，直接填充现有 output/result，再由 `hash_table` 用
  `RPC_DB_RETURN_CODE` 包装。
- DB dispatcher `init()` 会启动 uv timer 并调用 `cluster_init`/`raw_init` 连接真实 Redis；`SCRIPT LOAD` 不在
  `init()` 本体，而在连接建立回调里执行，SHA1 存入 `db_script_sha1_`。使用默认内存 DB 时不应把该 dispatcher
  加入 fixture，也不执行 init，以彻底避开真实 Redis I/O 与 timer。
- 内嵌 Lua（`db_msg_dispatcher.cpp` 中 `kCompareAndSetHashTable`、`kAddListIndexHashTable`）已明确语义：
  - CAS：`real_version == 0` 即使期望版本非零也写入并返回版本 1；`except_version == real_version` 写入并加一；
    否则返回 `CAS_FAILED|<real_version>`。**注意**：`CAS_FAILED` 到 `EN_DB_OLD_VERSION` 的映射与当前版本回写
    发生在 `db_msg_dispatcher::dispatch`，而 hash-table hook 会整体绕过该 dispatcher，因此内存 backend 必须
    自行复现该映射语义（错误码 + 回写 real_version）。
  - 列表追加：`HINCRBY index_number 1` 单调递增；当字段数 ≥ `max_len + 1` 时删除数值**最小**的索引，再写入新索引。
- UUID 公共入口（`src/server_frame/rpc/db/uuid.*`）包含 `generate_standard_uuid`、
  `generate_standard_uuid_binary`、`generate_short_uuid`、`generate_global_increase_id`、
  `generate_global_unique_id`。前三类**不访问 DB**；`generate_global_increase_id` 经生成代码
  `uuid_allocator::inc_field_auto_inc_id` 走 DB `inc_field`；`generate_global_unique_id` 内含静态号段缓存
  （`g_unique_id_pools` + `spin_rw_lock`，含 alloc_task/wake_tasks 协程排队）再调
  `generate_global_increase_id`。因此只 hook DB `inc_field` 无法覆盖前三类，必须在所有公共 UUID 分配入口统一
  hook；统一 hook 后生产静态号段缓存自然不会被触发。

### 2.8 资源加载

- 生成配置 manager 已提供 buffer loader、version loader、reload 和 group callback。
- `excel_config_wrapper_reload_all(is_init)`（`src/server_frame/config/src/excel_config_wrapper.cpp`）的准确
  行为：仅在**首次初始化**（`!get_excel_config_manager_inited()` 且 `logic_cfg().excel().enable()` 为真）时
  调用一次 `set_buffer_loader`/`set_version_loader`/`set_on_log` 并包装 group 回调；此后每次调用只做
  `set_override_same_version`/`set_group_number`/`set_on_not_found` + `reload_all(true)`。当前 loader 回调直接
  读 `excel().bindir()` 下的文件。结论：预先安装 loader 会在首次 init 时被覆盖，仅在测试开始前调 manager
  setter 不能稳定接管资源。
- 合适的 seam 是在 `excel_config_wrapper` 内部增加 scoped active provider，并让
  `excel_config_callback_get_buffer` / `excel_config_callback_get_version` 优先读取该 provider：测试提供内存
  path-to-bytes/version，wrapper 仍调用真实 manager 的解析、索引和切组逻辑。fixture 配置中资源用例需打开
  `excel().enable()`。
- wrapper 安装的 production loader/group wrapper 及 `get_excel_config_manager_inited()` 是进程生命周期状态；现有
  `excel_add_on_group_loaded_callback` 没有 remove API。因此 teardown 只能清除/恢复 scoped active provider，
  不能承诺恢复 manager 的 loader 或删除既有 group callback；工具也不得按 case 注册不可撤销 callback。

### 2.9 其余 RPC/传输清单（已核实）

| 模块 | 位置 | 最终 transport | 覆盖方式 |
| --- | --- | --- | --- |
| router | `src/server_frame/router/` + `rpc_call_api_for_ss.cpp.mako` router 分支 | SS（经 router_manager 寻址） | test manager/object + SS mock |
| rank | `src/component/rank/.../rankboardservice.atfw.gen.cpp` | SS（持久化另走 DB） | SS/DB 基础 mock |
| dtmq | `src/component/dtmq/` | SS（WAL 另走数据通道） | SS mock |
| distributed_transaction | `src/component/distributed_transaction/` | SS | SS mock |
| user | `src/server_frame/rpc/user/user_basic.*` | DB（`allocate_user_id`） | DB/UUID mock |
| auth/login | `src/server_frame/rpc/auth/login.*` | 无网络传输（纯工具函数） | 无需 mock |
| telemetry | `src/server_frame/rpc/telemetry/` | 非消息 RPC；OTLP HTTP / Prometheus push/file exporter | 只观测，不 mock |
| orbit server/controller/agent | `src/component/orbit/sdk/**` | 生成 RPC 最终走 `ss_msg_dispatcher`；server-to-client 也由 `orbit_server_manager` 封装为 SS | SS mock + 代表性契约测试 |
| Orbit client SDK | `src/component/GameSharedComponent/Orbit` | `OrbitClientRuntime::send_message` 最终直接 `get_bus_node()->send_data`；含 callback/retry pending map | 独立 `mock_orbit` adapter（见 8.8） |
| HTTP/curl | HPA controller、etcd 工具、telemetry exporter | `util::network::http_request`（存量老名）/ OTLP HTTP | 组件扩展；HPA pull 专用默认 hook 见 8.10 |
| echosvr 等示例 | `src/echosvr/app/echosvr_main.cpp` | atbus 直连 demo | 不作为内置 RPC 覆盖目标 |

### 2.10 外部组件依赖与离线性（已核实）

`atapp::app::init` 的顺序为：reload（仅本地 YAML/环境变量）→ 模块 setup → `setup_log` → `setup_atbus` →
`setup_curl_multi`（仅创建共享句柄，无连接）→ 模块 reload/init/ready。在此基准上，各外部依赖的触发条件与
fixture 处置：

| 依赖 | 触发条件 | init/运行行为 | fixture 处置 |
| --- | --- | --- | --- |
| etcd | `atapp.etcd.enable` + `atapp.etcd.hosts` 非空 | 启用时 `service_discovery_module::init` 经 `check_keepalive_actor_start_success` **阻塞 `uv_run`** 发真实 HTTP（member/list → lease/grant → keepalive）；未启用时全链路惰性、零网络 | fixture 一律不配 etcd；服务发现用 global discovery 注入替代。orbit agent 强依赖 etcd（未启用时 init 直接失败），v1 不支持其 fixture |
| atbus listen | `atapp.bus.listen` | 为空时零 socket；含**主机名**（非数字 IP）时 listen/connect 会在 libatbus io_stream 触发 `uv_getaddrinfo` | 默认空 listen（见 2.2）；需要时用数字 IP loopback 或 `shm`/`unix` |
| atbus upstream | `atapp.bus.proxy` | init 阶段**阻塞连接 atproxy** 直到 `initialize_timeout` | fixture 禁止配置 |
| Redis | `db_msg_dispatcher::init` 按 `logic.db.cluster/raw` | dispatcher init 即发起连接；hosts 为空不连 | fixture 不 init 该 dispatcher（见 2.7/8.5） |
| telemetry exporter | 各 exporter 按类型不同（endpoint / file_pattern / ostream 非空 / push 需 host+port+jobname / pull 需 url） | 无 exporter 时 provider 回退 Noop，零网络；OTLP gRPC 为 lazy connect；`prometheus_pull` 会开**入站**监听端口 | 默认留空（noop）或仅 file 类导出（见 8.9） |
| HPA | `logic.hpa.controller.enable` + `logic.hpa.metrics.enable`（默认均 false） | 关闭时不建任何 policy/puller/discovery，零网络；controller 对象与 curl share/multi 句柄无条件创建但无 IO；禁用时 reload 仍会写本地 app metadata 标签 | 默认关闭；测试启用时由默认安装的 pull hook 接管（见 8.10） |
| DNS | 运行时 `rpc::dns::lookup`；atbus 主机名 listen/connect | 见 2.5 与上两行 | DNS hook 接管；listen 避免主机名 |
| 日志 | `atapp.log` sink 配置 | 内建 sink 仅 file/stdout/stderr/syslog（本地接口），server_frame/component 无远程 sink | 无需处理 |
| 时间源 | 无 | 本地系统时钟（`atfw::util::time::time_utility`），无 NTP/远程同步 | 无需处理 |
| 组件 SDK init | dtmq/rank/distributed_transaction/orbit server SDK | init 均不触网（discovery 查询与 SS RPC 运行时按需）；Orbit client SDK init 主动 atbus 连接但属 client 侧 | 无需特殊处理 |

结论：最小 fixture 默认配置（无 etcd、空 bus.listen、无 bus.proxy、无 telemetry exporter、HPA 关闭、不含 db
dispatcher、DNS 已 hook）下，app init + ready + 空转 tick 全程不触网。上表所有触网开关必须在 fixture 配置
中保持关闭或已被 hook 接管，README 需完整列出该清单。

## 3. 核心设计决策

### 3.1 服务发现注入放在 atapp 层

选择：把 mock discovery 节点写入 atapp 的 global discovery，并注册 `mock://` connector；不在
`logic_server_setup.*` 增加伪节点或短路。

原因：

- discovery 数据的所有权和 endpoint/connector 选择都在 atapp；从这里注入能同时验证按 id、name、type/zone、
  metadata 和 broadcast 的真实选择逻辑。
- `logic_server_setup` 是服务装配策略，不是 discovery 数据源：server frame 内没有任何 connector 注册代码
  （`src/server_frame` 中 grep `connector` 零命中），只消费 atapp 托管的 connector。把 mock 放在
  `logic_server_setup` 会让测试依赖某个 service main，并绕过 atapp 的节点更新、索引和发送链。
- 使用已公开的 add-node/add-connector 扩展面（libatapp 单测已大量使用 add-node），预计不需要修改 vendored
  libatapp；测试代码只新增 connector 子类。

`mock://` connector 收到 outbound 后只入队，不在发送调用栈内同步回包。runtime 下一轮 pump 再投递 send-ack 和
模拟响应，因为调用 task 必须先执行到 wait 并登记 sequence；同步回包会产生“响应早于 waiter”的竞态。

### 3.2 hook seam 与 mock 策略分离

server frame 只加入最小、无策略的 seam；匹配规则、内存数据、脚本响应、历史记录和诊断全部放在
`src/tools/rpc-unit-test`。生产逻辑不依赖测试库。

新增构建开关：

```cmake
PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
```

- 当 `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` 为真时默认 `ON`，其他情况默认 `OFF`（用
  `cmake_dependent_option` 或等价的集中布尔条件实现），与本工具的 `add_subdirectory`/CTest 门控保持一致。
- 通过 `server_frame_build_feature.h.in` 增加 `#cmakedefine01` 项暴露一致宏（该文件由
  `generate_config_codes.cmake` 以 `configure_file(... @ONLY)` 生成到临时目录，再 `copy_if_different` 进
  生成头目录并加入 `server_frame-config` 库），不能只用 `NDEBUG`：Release 单元测试也应可用，多配置构建也不能
  因目标宏不一致产生 ODR/ABI 风险。
- `server_frame`/`server_frame-config` 使用上述生成宏；Orbit SDK 不新增对 config target 的依赖，而由其 CMake
  从同一 option 派生并 PUBLIC 传播 `ORBIT_CLIENT_SDK_ENABLE_UNIT_TEST_HOOKS=0/1`，保证 SDK 与消费者声明一致。
- `OFF` 时 hook registry、setter 和热路径分支不进入产物，保持生产运行时零额外分支和零状态。
- `ON` 时仍保持 production fallback；只有 fixture 显式安装 hook 后才接管。

hook ABI 与状态所有权必须同时满足：

- 每个 seam 的 registry 只在其**拥有代码的 production target/DLL** 的一个 `.cpp` 中定义：核心
  SS/DNS/CS/DB/UUID 在 `server_frame`，Excel provider 在 `server_frame-config`，Orbit client 在其 SDK target；安装/
  清除函数使用各自导出宏。这样遵守 `server_frame -> server_frame-config` 的既有单向依赖，不制造 config 反向依赖，
  同时确保 Windows shared build 每个 seam 只有一份状态。
- 禁止 header-local static、inline global、跨 DLL 复制 registry，或由测试库持有 production 全局入口。
- server frame 的公开头只暴露最小 provider/request/result 协议，不 include 测试工具类型，也不依赖测试库；
  非模板公开实现放 `.cpp`，测试库的公开 API 另设 `RPC_UNIT_TEST_API` 导出宏。
- `#if PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` 包住 registry、lookup 和调用点，且不得改变既有公开 class 的
  数据布局。OFF 构建不留下符号、状态或热路径分支。
- registry 保存 shared state + generation token，异步队列只保存 owned bytes/message/metadata；不得保存 fixture、
  protobuf Arena、`gsl::span`、`string_view` 或 endpoint 的悬空引用。

### 3.3 真实 task + 同步测试函数事件泵

用户的 `CASE_TEST` 保持普通同步函数。`runtime::run_task` 内部调用 `rpc::async_invoke`，返回统一的
`task_handle`；`runtime::wait` 循环执行以下工作：

1. `atfw::util::time::time_utility::update()` 刷新缓存时钟（task 超时与 TTL 依赖它）；
2. `app.run_noblock()` 推进公共模块和 task manager，并收集本代产生的 outbound；
3. 只投递**上一代及更早**已经到期的 connector、DNS、CS 事件，零延迟响应也至少跨一次 pump generation；
4. 再执行一次有界 `app.run_noblock()`，让 dispatcher 恢复 task；
5. 用 `task_type_trait` 判断成功、取消、fault 和 task timeout；
6. 检查独立的 wall-clock hard deadline。

task 自身 timeout 和 `wait` hard timeout 必须同时存在。前者验证业务超时，后者防止配置、时钟或 dispatcher 异常
导致 CTest 挂死。hard timeout 时调用 `task_manager::kill_all`，把 runtime 标记为 poisoned 并立即进入受限时的
stop/drain；该 runtime 不得复用。事件泵使用有界批次和 thread yield，不长时间 sleep，也不忙等占满 CPU。

### 3.4 默认行为：单向通知成功丢弃，需响应立即报错

- 通用规则（两类 RPC 的默认行为不同）：
  - **单向通知类 RPC**（SS stream/no-wait/broadcast、CS 下行 data/kickoff/set-router/broadcast 等不需要
    response 的调用）：用户未注册 mock 时默认**记录调用并直接正常返回**，结果丢弃；
  - **需要 response 的 RPC**（SS unary 及 wait 变体、DNS lookup、HPA metrics pull 等）：用户未注册 mock 时
    默认**立即返回明确错误码**并输出诊断（RPC 名、目标、sequence、metadata 和可用规则），提示必须补充 mock
    才能完成完整测试，不允许静默等待到超时。
- DB：默认是空的内存数据库，未命中读取返回项目既有的 record-not-found；所有已支持操作都有真实内存语义。
- UUID：默认确定性且每个 fixture 重置。
- Resource：未提供路径按真实 loader 的 missing/error 语义失败。
- 每类 mock 可显式覆盖默认值，选择 `fail`、`record-only/drop`、`timeout` 或 `passthrough`；passthrough 默认
  关闭。

### 3.5 mock 代码分布与 `mock` 子命名空间约定

- `src/tools/rpc-unit-test` 只放公共基础设施：runtime/事件泵、mock connector、通用规则引擎与内存 backend
  （SS/DNS/CS/DB/UUID/resource 的 registry、匹配、历史和诊断）。
- 面向具体 service/功能的 mock 接口不放进工具目录，而是放在**各自模板生成的代码或各自的实现代码**中，并
  统一放入被 mock API 同级的 `mock` 子命名空间，便于管理和 IDE 代码提示：
  - 生成代码：`rpc_call_api_for_ss.*.mako` 生成 `<service>::mock` 的 typed 注册辅助（见 8.2）；
    `db_interface` / `db_rpc_redis_kv/kl` 生成 `<db 命名空间>::mock` 的 per-table typed 接口（数据准备、
    检查与行为注入，见 8.5）；
  - 手写实现：HPA pull mock 放在 server_frame HPA 实现目录的 `mock` 子命名空间（见 8.10）；UUID provider
    注册接口放在 `rpc::db::mock`（见 8.6）。
- 这些 mock 接口全部用 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`（Orbit SDK 用其派生宏）整体门控，
  hooks-off 生产构建中不存在对应符号；它们只是 server_frame seam/注册表之上的 typed 适配层，不改变
  “策略与数据在工具库”的边界（见 3.2）。
- 生成代码中的 mock 接口属于生成物的一部分：只改 Mako 模板并经现有生成流程重建，不手改 `.atfw.gen.*`。
- 命名以 `atfw::util` 为准（如 `atfw::util::network::http_request`、`atfw::util::time::time_utility`）：本工具库、
  新增 mock 接口与文档一律使用 `atfw::util::` 前缀，不引用无 `atfw::` 前缀的老别名。注意存量 server_frame
  代码（如 HPA 的 `create_http_request` 返回 `util::network::http_request::ptr_t`、`ss_msg_dispatcher`/
  `cs_msg_dispatcher` 部分仍用 `util::time::time_utility`）大量依赖 legacy alias
  （`namespace util { using namespace atframework::util; }`，受 `ATFRAMEWORK_UTILS_ENABLE_LEGACY_ALIAS` 控制）；老名与
  `atfw::util::` 是**同一类型**，接线时无需转换即可互操作。本计划不顺带重排这些存量老前缀（尊重脏工作区、
  避免无关改动），只要求新代码统一 `atfw::util::`。

### 3.6 符号可见性与 ABI 规则

工具库和生成的 mock 接口会被外部测试代码（含服务自身的单测）链接使用，必须遵守：

- 头文件里的模板函数必须要么 `ATFW_UTIL_FORCEINLINE`，要么 `ATFW_UTIL_SYMBOL_VISIBLE`，二者必居其一
  （现有 `runtime::run_task`/`wait` 的 duration 转发模板、`mock_db::register_message_type` 即按此标注）。
- 库的头文件新增类型时，如果要被外部使用，要正确处理符号可见性（class/struct 用
  `ATFW_UTIL_SYMBOL_VISIBLE` 或 `RPC_UNIT_TEST_API`，自由函数同理），且必须考虑跨平台、跨编译器兼容性：
  GCC/Clang `-fvisibility=hidden`、MSVC `__declspec(dllexport/dllimport)`、MinGW 与 Apple Clang 均须通过；
  不新增仅 MSVC 可导出的写法（如依赖 `__declspec(selectany)` 的静态数据成员模板）。
- 公共 mock 接口（含 SS rule engine）**不暴露模板实现**：与用户交互的 API 一律保持导出函数形式——声明在
  头文件、具体实现放 cpp，不对用户暴露内部细节（invoker、descriptor 查找、prototype 创建等）。typed
  便捷由生成层提供为**具体的非模板函数**（`rpc_call_api_for_ss.*.mako` 按 service 生成
  `<service>::mock` 的导出函数，内部完成类型适配），而不是头文件 C++ 模板。
- 生成 mock 代码编译进 server_frame（及服务自身的生成 TU），因此**禁止直接链接工具库符号**；引擎调用
  一律经 `rpc/unit_test/mock_engine_bridge.h` 的类型擦除桥接槽转发（server_frame 自有镜像类型
  `ss_mock_rule_options`/`ss_mock_request_view`/`mock_rule_handle`，规则停用用 `shared_ptr<void>` token
  的 deleter 传递），由 `mock_ss::bind`/`mock_db::bind` 注册、unbind 清理。桥接为空时生成 helper 降级为
  空 handle/false/no-op。引擎直连接口（`mock_ss::mock`、`mock_db::mock_table` 等）仍保留给测试直接使用。

## 4. 目标架构

```text
CASE_TEST
  |
  +-- rpc_unit_test::runtime
      +-- atapp::app + logic_server_common_module + task_manager
      +-- mock:// raw transport + real atapp discovery/index
      +-- async_invoke task + run_noblock/time-update/pump + hard timeout
      |
      +-- SS  --> real generated API --> SS dispatcher --> mock connector --> queued real dispatcher response
      +-- router -> router_test_manager/object --> SS dispatcher --> mock connector（与 SS 同链）
      +-- DNS --> real dns::lookup ----> DNS hook --------> queued custom_resume
      +-- CS  --> real session/API ----> CS send hook ----> captured gateway message
      +-- DB  --> generated DB API ----> hash_table hook -> in-memory backend
      +-- UUID -> public allocator ----> UUID hook -------> deterministic allocator
      +-- XLSX -> excel wrapper -------> resource hook ---> in-memory bytes/version --> real manager
      +-- Telemetry -> fixture 配置即 noop/本地文件（无需 hook，见 8.9）
      +-- HPA --> policy/puller -----> 默认 pull hook ---> 预置指标（见 8.10）
```

runtime 的最小模块集默认只包含 `logic_server_common_module`，并开启 task manager。`runtime_options.features`
决定 fixture 配置中的 `logic.server.shared_component` 与实际 module 集合，两者必须一致；CMake 的 `FEATURES` 只负责
链接依赖、参数校验和标签，不隐式决定每个 case 的 runtime 行为。按 C++ 选项再启用：

- SS：`ss_msg_dispatcher`；
- CS：`cs_msg_dispatcher` 和 session manager；
- Resource：Excel/config manager 初始化（fixture 配置打开 `excel().enable()`）；
- Router RPC：配对的 `router_test_manager` + `router_test_object`（manager 构造即自动注册进
  `router_manager_set`，object 预置 mock router server id）；
- DB：只安装 hash-table hook，不初始化 `db_msg_dispatcher`；
- DNS：使用现有 SS dispatcher 的 DNS 类型信息和独立 DNS hook，不访问 libuv resolver；
- Telemetry：fixture 配置默认无 exporter（noop），可选 file-only 导出（见 8.9）；
- HPA：默认关闭；`runtime_options` 启用 HPA feature 时自动安装默认 pull hook（见 8.10）。

runtime 提供 setup callback，让组件添加自己的 main module、dispatcher 或 router manager，而不复制服务进程的
main。

## 5. 拟新增目录与文件

实施后目录预计如下；名称可在编码前按现有命名规则微调，但职责不得混合：

```text
src/tools/rpc-unit-test/
  CMakeLists.txt
  IMPLEMENTATION_PLAN.md
  README.md
  cmake/
    ProjectRpcUnitTest.cmake
  include/atframework/testing/
    runtime.h
    task_wait.h
    mock_discovery.h
    mock_connector.h
    mock_ss.h
    mock_router.h
    mock_dns.h
    mock_cs.h
    mock_db.h
    mock_uuid.h
    resource_provider.h
    raw_transport_adapter.h
    mock_orbit.h
  src/
    runtime.cpp
    task_wait.cpp
    mock_connector.cpp
    mock_ss.cpp
    mock_router.cpp
    mock_dns.cpp
    mock_cs.cpp
    mock_db.cpp
    mock_uuid.cpp
    resource_provider.cpp
    mock_orbit.cpp
  test/
    CMakeLists.txt
    rpc_unit_test_smoke.cpp
    rpc_unit_test_ss.cpp
    rpc_unit_test_cs.cpp
    rpc_unit_test_db.cpp
    rpc_unit_test_resource.cpp
    rpc_unit_test_orbit.cpp
    config/
      rpc-unit-test.yaml
    protocol/
      rpc_unit_test.proto
```

`rpc_unit_test.proto` 是必需的测试输入：内置 `router_transfer` 足以验证普通 unary，但当前仓库没有启用中的
`router_rpc: true` 方法，无法覆盖所有 Mako 分支。该 proto 通过工程现有 protobuf/RPC helper 生成到构建树，至少
实例化 router RPC，并补齐 user、no-wait/wait-later 等缺失组合；生成输出不手工提交或修改，生成命令必须使用稳定
`OUTPUT`/`BYPRODUCTS`/`DEPENDS`，内容不变时不更新时间戳。

工具目录只包含公共代码（见 3.5）；各 service/功能的 typed mock 接口随各自实现或模板生成代码落地（SS/DB
模板、server_frame HPA/UUID 实现目录），不在上表列出。

### 5.1 预计修改的工程文件

除新增工具目录外，实施阶段允许且预计只在下列源文件增加窄 seam/装配；编码前仍需以符号检索确认最终文件名：

| 范围 | 预计文件 | 修改目的 |
| --- | --- | --- |
| 构建开关 | `project/cmake/ProjectBuildOption.cmake`、`src/server_frame/config/include/config/server_frame_build_feature.h.in` | 增加 hooks-on/off feature |
| 装配 | `src/CMakeLists.txt` | 在 component 之前门控引入 helper/target |
| 核心 hook registry | `src/server_frame/testing/` 下新增内部 header/cpp，并接入 `server_frame.cmake` | `server_frame` DLL 内唯一状态、导出安装/清除协议 |
| DNS | `src/server_frame/dispatcher/ss_msg_dispatcher.*` | 在 `uv_getaddrinfo` 前增加可选 seam |
| CS | `src/server_frame/dispatcher/cs_msg_dispatcher.*` | 收敛 gateway-send 并增加可选 seam |
| DB | `src/server_frame/rpc/db/hash_table.*` | 在本地校验后、Redis 参数构造/发送前增加同步 seam |
| DB 测试身份 | `src/server_frame/dispatcher/db_msg_dispatcher.*` | hooks-on 时只设置既有 prefix/channel 字段，不 init timer/连接 |
| UUID | `src/server_frame/rpc/db/uuid.*` | 五个公共入口增加 provider seam；安装/覆盖 API 放入 `rpc::db::mock` |
| HPA | `src/server_frame/logic/hpa/pull/prometheus/logic_hpa_puller_prometheus.*`、同目录新增 mock 接口文件 | `do_pull` 入口 seam + `mock` 子命名空间的指标注入 API（宏门控） |
| SS mock 接口 | `src/templates/rpc_call_api_for_ss.*.mako` | 增量生成 `<service>::mock` typed 注册辅助（宏门控纯新增，不改热路径） |
| DB mock 接口 | `src/templates/db_interface.*.mako`、`src/templates/db_rpc_redis_kv.*.mako`、`src/templates/db_rpc_redis_kl.*.mako` | 增量生成 `<db 命名空间>::mock` per-table typed 接口：数据准备、检查与行为注入（宏门控纯新增） |
| 资源 | `src/server_frame/config/include/config/excel_config_wrapper.h`、`src/server_frame/config/src/excel_config_wrapper.cpp` | provider registry 归属 `server_frame-config` DLL，回调内优先 scoped provider |
| 生成覆盖 | `src/tools/rpc-unit-test/test/protocol/rpc_unit_test.proto` + 现有生成 helper | 只生成到构建树，覆盖当前未实例化分支 |
| Orbit adapter | `src/component/GameSharedComponent/Orbit/include/Orbit/OrbitClientRuntime.h`、`src/component/GameSharedComponent/Orbit/src/Orbit/OrbitClientRuntimeRpc.cpp` | SDK target 自有 registry；由全局 option 派生公开 compile definition，不依赖 server-frame config |

普通 SS 与 router 传输无需新增 dispatcher/template hook；`rpc_call_api_for_ss.*.mako` 和
`session_downstream_api_for_cs.*.mako` 的传输路径先做覆盖审计，仅在证明确有分支绕过统一 seam 时才修改并统一
重新生成。模板另按 3.5 约定增量生成宏门控的 `mock` 子命名空间接口（纯新增声明，不改既有调用路径）。

## 6. CMake 接入设计

### 6.1 公共函数

提供一个主入口：

```cmake
project_add_rpc_unit_test(
  TARGET component_name_rpc_test
  COMPONENT component-name
  SOURCES test_a.cpp test_b.cpp
  LINK_LIBRARIES components::component_name_sdk
  FEATURES SS DNS DB
  LABELS component-name fast
  TIMEOUT 20
)
```

参数：

- `TARGET`、`COMPONENT`、`SOURCES`：必填；每个组件生成独立 executable，`COMPONENT` 用于稳定 label/folder，
  不从目录名或 target 字符串猜测。
- `LINK_LIBRARIES`：组件 SDK、协议和附加实现 target；要求传入真实 target/alias，避免依赖易拼错的裸库名。
- `FEATURES`：`SS`、`DNS`、`CS`、`DB`、`UUID`、`RESOURCE`、`ROUTER`、`ORBIT`；用于校验可用性、链接最小
  依赖和生成标签。`ORBIT` 仅在对应 component target 存在时链接独立 adapter，否则 configure 阶段给出明确错误；
  case 仍通过 `runtime_options.features` 明确启动模块。
- `LABELS`：附加 CTest 标签。
- `TIMEOUT`：CTest executable 进程级超时；同一 target 含多个串行 case 时，必须大于各 case hard-timeout + teardown
  deadline 的最坏总和，而不只是大于单个 runtime timeout。
- `CONFIG`、`WORKING_DIRECTORY`、`ENVIRONMENT`：可选，仅在组件确有特殊启动配置时使用。

函数职责：

- 复用本目录集中创建的 private-main OBJECT target 与 private-frame STATIC/OBJECT target，绝不调用可能选择框架的
  自动 helper，也不让每个 executable 重复编译 frame 源码。
- 链接 `${PROJECT_NAME}::rpc-unit-test`、`${PROJECT_SERVER_FRAME_LIB_LINK}` 及调用者依赖。
- 复用工程现有编译选项、统一运行时输出目录、RPATH 规则和 IDE folder。
- 创建 `add_test(NAME <target>.unit COMMAND $<TARGET_FILE:<target>>)`。
- 默认标签：`${PROJECT_NAME};unit;rpc-unit-test`，再添加 `component:<name>`、`feature:ss`、`feature:db` 等稳定标签及
  调用者标签。
- 设置 CTest `TIMEOUT`；需要独占外部资源的显式 passthrough 测试另设 resource lock，不污染普通内存测试。
- 使用 generator expression 获取 executable，避免假设输出目录或配置名。
- 默认 `WORKING_DIRECTORY` 为 target 对应的 build-tree 目录；`CONFIG` 通过 `copy_if_different` 或明确生成规则放入
  该目录。测试不得把运行日志、临时配置或诊断 dump 写到源码树/仓库根。
- 对 executable 按 `ItemAlgorithmTest` 的现有方式调用
  `project_setup_runtime_post_build_bash(<target> PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_BASH)` 与
  `project_setup_runtime_post_build_pwsh(<target> PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_PWSH)`；CTest 使用
  `ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:<...>"` 注入统一 bin 和依赖目录，兼容多配置生成器。README
  另说明直接运行 executable 所需 PATH，不复制 DLL。

命名在本计划中已关闭：CMake API 使用 `project_add_rpc_unit_test`，库为 `${PROJECT_NAME}-rpc-unit-test`，alias 为
`${PROJECT_NAME}::rpc-unit-test`；C++ 使用 `atframework::testing`，不再把命名决策推迟到实现阶段。

### 6.2 CMake 装配顺序

修改 `src/CMakeLists.txt`，在 `server_frame` 之后、`component` 之前，且仅在 `BUILD_TESTING OR
PROJECT_ENABLE_UNITTEST` 时添加 `tools/rpc-unit-test`。`src/tools/CMakeLists.txt` 不重复添加该目录，其余工具
顺序保持不变。

这样组件自己的 CMake 在 configure 阶段已经能看到函数和支持库 target，不依赖“稍后声明 target”的隐式行为。

所有未来实现产生的 configure/build/test 临时文件必须放入已解析的 `<BUILD_DIR>`；当前工作区即
`build_jobs_cmake_tools`（由 `.vscode/settings.json` 的 clangd `--compile-commands-dir` 与既有构建树解析），
agent scratch 放在 `build_jobs_cmake_tools/_agent_tmp`。

## 7. C++ 用户接口设计

以下代码是接口草图，不是本阶段实现：

```cpp
CASE_TEST(component_rpc, db_dns_ss) {
  atframework::testing::runtime test;

  atframework::testing::runtime_options options;
  options.features = {atframework::testing::feature::ss, atframework::testing::feature::dns,
                      atframework::testing::feature::db};
  int start_result = test.start(options);
  CASE_EXPECT_EQ(0, start_result);
  if (start_result != 0) {
    return;  // 私有框架只有 non-fatal CASE_EXPECT_*，必须显式短路。
  }

  atframework::testing::mock_node remote_node;
  remote_node.set_id(0x120001)
      .set_name("unit-test-remote")
      .set_type_name("logic")
      .set_ingress("mock://unit-test-remote");
  auto remote = test.discovery().add_node(remote_node);

  test.dns().answer("rpc-unit-test.invalid", {
      atframework::testing::address::v4("192.0.2.1"),
      atframework::testing::address::v6("2001:db8::1"),
  });

  test.ss().mock("package.Service/method",
      Request::descriptor()->full_name(), Response::descriptor()->full_name(),
      [](const atframework::testing::ss_request_view& request, google::protobuf::Message& response) {
        auto& typed_response = static_cast<Response&>(response);
        return 0;
      });

  auto task = test.run_task("db_dns_ss", std::chrono::seconds{2},
      [&](rpc::context& ctx) -> rpc::result_code_type {
        // 直接调用现有生成 RPC，并继续使用 RPC_AWAIT_* / RPC_RETURN_CODE。
      });

  auto result = test.wait(task, std::chrono::seconds{3});
  CASE_EXPECT_FALSE(result.task_timed_out);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(0, result.result_code);
  CASE_EXPECT_EQ(1, test.ss().calls("package.Service/method"));
  CASE_EXPECT_EQ(0, test.stop());
}
```

接口原则：

- `runtime` 构造不做隐式启动；`start()` 返回可断言的错误，析构只做兜底清理。
- `stop()` 幂等并返回 expectation/teardown 结果；正常 case 必须显式断言 `stop()`，析构只处理异常退出或遗漏。
  `start()` 中途失败也要按已完成阶段逆序回滚，不留下 active-runtime guard。
- 所有公开非模板类型/函数使用工具库导出宏，定义放 `.cpp`；接口和示例保持 C++17 可编译，不用 designated
  initializer，使同一源码也能用于传统协程配置。
- `run_task` 只接受能由 `rpc::async_invoke` 执行的 callable，始终创建真实 task。
- `wait_result` 至少包含 result code、task status、task-timeout、hard-timeout、runtime-poisoned、elapsed、task id
  和诊断文本，避免把两种 timeout 混成一个布尔值。
- 测试断言优先放在 task 完成之后；task 内通过返回码传递业务失败，避免测试框架的 thread-local/生命周期语义与
  协程切换耦合。
- 私有框架只有 non-fatal `CASE_EXPECT_*`；`start()` 等前置条件失败后示例必须显式 return/cleanup，不能假设断言
  会中止 case。
- mock 注册使用 protobuf descriptor 校验完整 RPC 名、输入和输出类型；错误配置在测试启动时失败。
- 所有 registry 返回 RAII handle；handle 销毁或 runtime teardown 后规则失效，不保留裸回调指针。
- `test.transport()` 暴露 raw send rule/history，`test.ss()` 只是建立在其上的 typed 协议层；用户可直接断言 atapp
  按 id/name/discovery/consistent-hash/random/round-robin 的目标选择、message type、sequence、payload 和 metadata。

## 8. 各类 mock 的实施细节

### 8.1 服务发现与 mock connector

实现 `mock_connector`（`atapp_connector_impl` 子类），构造中 `register_protocol("mock")`，并将 discovery 节点
ingress 设置为 `mock://<endpoint>`。实现要点（对应 2.2 的约束）：

- `get_address_type` 与 loopback 一致返回 duplex/local-host/local-process；`on_start_connect` 必须 retain handle、
  `set_ready()` 并设置可校验的 private data，`on_close_connection` 按同一 key 对称清理；以内置
  `atapp_connector_loopback` 为最近参照（无现成自定义 connector 先例）。
- 注入/删除/更新具有 id、name、type id、type name、zone、metadata、version 的真实 discovery node
  （`make_strong_rc<etcd_discovery_node>` + `copy_from`）。
- connector 队列拥有 payload、metadata、地址/节点标识和响应数据；只在投递调用栈内临时形成 span/string-view，
  不保存借用视图或 endpoint 裸指针。
- raw transport 层记录 endpoint 连接、发送目标、message type、sequence、payload 和 metadata，并提供独立规则/
  history；SS outbound 再交给上层 SS rule engine。
- 模拟远端入站时，`message_sender_t` 使用远端 discovery 的 id/name/direct-source/endpoint，而不是本地 app 身份；
  send-ack 使用对应 connection handle，避免测试因错误 sender 仍“碰巧”通过。
- 支持并契约测试按 id/name/discovery-node/consistent-hash/random/round-robin/type-zone 查找与 broadcast 数量；
  所有响应至少延后一代 pump，且 teardown 丢弃 generation 已失效的事件。
- teardown 时从 global discovery 删除本 fixture 节点，断开 connector，清空待投递事件。

### 8.2 SS RPC

普通 SS 使用 transport-level typed rule。engine 公共 API 为非模板导出函数（见 3.6），typed 便捷由
`rpc_call_api_for_ss.*.mako` 生成的 `<service>::mock` 具体函数提供：

```cpp
test.ss().mock(full_rpc_name, Request::descriptor()->full_name(), Response::descriptor()->full_name(),
               [](const atframework::testing::ss_request_view& request, google::protobuf::Message& response) -> int {
                 const auto& req = static_cast<const Request&>(request.body);
                 auto& rsp = static_cast<Response&>(response);
                 return 0;
               });
// 生成层等价快捷方式：<service>::mock::method(handler_typed)（非模板导出函数，内部完成上述适配）。
test.ss().expect(full_rpc_name).times(1).to_node(remote.id());
```

规则匹配维度：完整 RPC 名、目标节点、请求 protobuf 类型、metadata、user/router header；支持 FIFO 脚本和默认
handler。

发送调用栈复制 owned bytes/metadata，并只做无用户代码的 preflight：解析必要 SS header、匹配并预留规则；严格模式
未匹配和 transport send error 因而可立即返回。typed request materialization 与用户 handler 执行统一在后续 pump
generation 中发生；handler 输出也序列化成 owned bytes 后再回注，既消除 waiter 注册竞态，也避免持有调用 task
Arena 中的对象。

响应能力：

- 正常 response body；
- RPC error code；
- transport send error；
- 延迟响应、不响应以触发 timeout；
- malformed type/body；
- stream/no-wait 仅记录；
- `allow_no_wait` 与 `wait_later`；
- broadcast 每个目标独立记录/响应策略。

默认行为遵循 3.4：stream/no-wait/broadcast/wait-later 属于单向通知类，未匹配时默认记录并成功（结果丢弃）；
unary 及 wait 变体需要 response，未匹配时默认立即失败并输出诊断。两类默认值都可用 per-rule/per-engine 选项
覆盖（例如把某个 stream 显式设为 `fail` 做负向断言）。

dispatcher 回注必须走 app 的真实 forward event 和 `ss_msg_dispatcher`，不能直接完成 task promise。具体分两条
独立通道：

- **回包 / 远端主动消息**：用 `app::trigger_event_on_forward_request(sender, reply_ss_bytes)` 注入一条入站
  `message_t`，其 `SSMsgHead::destination_task_id` 必须等于请求的 source task id、`sequence` 与请求一致，由
  `app_default_handle_on_receive_request` → `ss_msg_dispatcher` 唤醒原 task；
- **投递结果 ack**：若被测代码检查发送是否成功，另用 connector 的 `on_receive_forward_response`（或
  `app::trigger_event_on_forward_response`）投递 send-ack。它与回包是不同通道，不能混用；transport send error
  也可由 `on_send_forward_request` 直接返回非零码模拟。

这样可以覆盖序列化、头字段关联、dispatcher 查找与 custom wait。

router RPC（基于 2.4 的核实结论，生产模板路径本身**不需要修改 Mako**）：

1. 实现 `router_test_object`（`router_object_base` 子类），补齐其**完整纯虚接口**：`name()`、
   `pull_object(ctx, priv_data)`、`save_object(ctx, priv_data)`、`save(ctx, priv_data, io_task_guard&)`（注意
   `pull_cache` 与不带 io_guard 的 `save(ctx, priv_data)` 是**非纯虚**便捷重载，无需实现），并预置
   `set_router_server_id(mock_node_id)`；所有持久化动作只返回项目成功码，不做 I/O。
2. 实现 `router_test_manager`（`router_manager_base` 子类）：构造/析构自动注册/注销，补齐其**完整纯虚
   接口**：`name()`、`get_base_cache(key) const`，以及**带 `io_task_guard&` 的**
   `mutable_cache`/`mutable_object`/`remove_cache`/`remove_object`（不带 io_guard 的同名版本是非虚便捷封装，
   会自建 io_guard 后转调纯虚版，不能误当作需实现的 seam），并把测试 key 同步解析到上述 owned object，不能
   返回空 cache，也不触发真实 cache 拉取。
3. 之后出站字节经 `ss_msg_dispatcher::send_to_proc` 被 mock connector 正常捕获，回包走与 unary SS 完全相同的
   注入通道，复用 SS rule engine 做匹配与响应。
4. 用 test-only proto 的 router/unary、router/stream 和 manager-not-found（未安装 test manager 时）fallback
   回归测试证明生产语义未漂移。

普通 SS、broadcast、user RPC 和 router RPC 的**传输路径**不在 Mako 中增加 mock 分支，避免在生成代码热路径
重复 hook；test-only proto 只用于生成覆盖。按 3.5/3.6 约定，SS rule engine 的公共 API 是非模板的导出函数
（`mock_ss::mock(full_rpc_name, request_type_name, response_type_name, handler, options)`，handler 操作
`google::protobuf::Message` 抽象基类，descriptor 校验、prototype 创建与 parse/serialize 全部在 cpp 内完
成）；`rpc_call_api_for_ss.*.mako` 再增量生成 `<service>::mock` 的**具体非模板** typed 注册辅助——宏门控
的纯新增导出函数，按 service/method 的已知 Req/Rsp 类型完成 `static_cast` 适配后经 `rpc::unit_test`
桥接槽注册到当前绑定的 SS engine（不直接链接工具库，见 3.6），并按 proto 的 rpc_options 自动套用 3.4 默认
行为；模板变更经现有生成流程重建并做双模式编译验证。

### 8.3 DNS RPC

在 `ss_msg_dispatcher::send_dns_lookup` 发起真实 `uv_getaddrinfo` 前增加单一 hook：请求包含 domain、task id、
sequence 和查询选项。

mock DNS 提供：

- A、AAAA、多记录和空结果；
- 项目错误码/系统解析错误；
- 延迟、乱序和 no-response；
- 调用次数和 domain 历史。

响应由 runtime pump 构造与 `dns_lookup_callback` 相同形状的 resume message（message type 为
`get_dns_lookup_rpc_type()`，msg_addr 指向 `std::vector<address_record>`），使用原 sequence 调用
`rpc::custom_resume`。vector/string backing storage 由队列项拥有并覆盖整个同步 resume 调用；若 task 已取消或
generation 失效，则丢弃并记录 late response。生产 fallback 继续使用当前 resolver，测试 hook 不直接设置 task
结果。

### 8.4 CS RPC

先将 `send_kickoff`、`send_set_router`、`send_data` 和多会话版 `broadcast_data` 四个当前各自调用
`owner->get_bus_node()->send_data(...)` 的下行分支收敛到 `cs_msg_dispatcher` 的一个统一 gateway-send 内部
函数，再在该函数上加可选 hook。所有 data/kickoff/set-router/broadcast 分支都必须通过这个函数；hook 未处理时
调用原 bus node。统一 request 会携带 `session_ids` 供测试观测，但 production fallback 必须保留多会话版当前忽略
该参数的既有行为；是否修复 broadcast 语义属于独立行为变更，不在增加测试 seam 时顺带修改。

提供 `mock_client`：

- 构造真实线格式的 `message_t`（`type == service_type::kAtGateway`，body 为 `atfw::gateway::server_message`），
  通过 `cs_msg_dispatcher::dispatch(source, kAddSession)` 触发 `session_manager::create` 建立真实 session；
- 打包 typed CS request（`CSMsg` 嵌入 `kPost`），用 `dispatch` 注入上行，触发实际 action task；
- 通过 `dispatch(..., kRemoveSession)` 关闭；
- 支持 `kSetRouterRsp` 上行封装，覆盖 session router 设置的完整请求/响应链；
- 从统一 gateway-send hook 捕获并解析 downstream `CSMsg`、kickoff、router 指令和 broadcast；
- 可注入发送错误、断开和多个客户端。

下行默认行为遵循 3.4：data/kickoff/set-router/broadcast 均属单向通知类，未注册捕获规则时默认捕获即成功
（丢弃）；上行注入始终由测试显式驱动。若覆盖审计要求 per-service typed 捕获辅助，则按 3.5 在
`session_downstream_api_for_cs.*.mako` 增量生成 `mock` 子命名空间接口。

`session_downstream_api_for_cs.*.mako` 的当前路径已经汇聚到 session/dispatcher，第一版不改模板。只有覆盖盘点
发现某个生成分支绕过统一边界时，才修改 Mako 并重新生成；该检查是实施阶段的强制 gate。

### 8.5 DB RPC

hook 放在每个 `hash_table` 公共操作完成 task、descriptor、字段、空索引和 message 等全部本地同步校验之后，
Redis 专用参数构造/首次发送之前。这样：

- 仍验证调用必须处于 task；
- 生成 DB API 的 key/type 逻辑得到覆盖；
- `batch_get_all`/`batch_partly_get` 由 `get_all`/`partly_get` 组合而成，hook 原语即自动覆盖 batch；
- 默认路径不初始化 Redis dispatcher；
- tracing 可在现有 span 内完成并保留真实错误码；允许 `db_msg_dispatcher::me()` 因 trace 属性被构造，但默认
  fixture 绝不调用其 `init()`，因而不连接 Redis、不启动其网络 timer。

生成 DB API 在进入 `hash_table` 前会读取 dispatcher 的 record prefix/channel（经 `get_record_prefix()` 与
`get_db_channel_type()`）；未 `init()` 时构造函数默认值分别为空串和 `channel_t::MAX`（注：`init()` 会把
`record_prefix_` 置为配置值/`get_stable_host_id(...)`/`"default"`，故不能依赖 init 后的“空串”）。默认 runtime
因而需在 hooks-on 下通过窄测试 API 把**既有字段**设置为确定性的 `unit-test`/`RAW_DEFAULT`（`RAW_DEFAULT` 是
`channel_t::type` 合法枚举），但仍不调用 `init()`；若检测到 dispatcher 已是非 `MAX`（说明外部已初始化），
应拒绝覆盖，除非用户显式选择 passthrough。teardown 恢复原值，此 API 不新增 class 数据成员。

为避免把 C++20 coroutine return type 暴露到 hook ABI，server-frame seam 使用同步的“handled + result code”
接口，直接填充现有 output/result 对象；`hash_table` 再用当前 `RPC_DB_RETURN_CODE` 返回。

内存 backend 的 key 至少包含 DB channel、规范 key 和 protobuf descriptor/type。值不能仅保存整条序列化 message：
Redis `HSET` 是按 present field 合并，`partly_get`/`HMGET` 也依赖字段存在性；应保存逻辑 field map，或实现等价的
presence-aware merge，并维护 CAS version、TTL，KL 另存单调索引与每项字段。seed API 必须声明 descriptor/type，
对同 key 的类型漂移快速失败。必须覆盖：

- KV：get-all、partly-get、batch-get（经原语组合）、set、带/不带 CAS、inc-field；
- KL：get-all、get-by-index、update-by-index、add-index、remove-by-index；
- 通用：remove-all、set-ttl、remove-ttl；
- 可控时钟下的 TTL 到期；
- protobuf 类型不匹配、缺失字段和序列化错误；
- result/error/调用历史注入。v1 seam 是同步接口，不承诺 DB 人工 latency/no-response；异步 timeout 由 SS/DNS
  覆盖。若未来需要 DB latency，必须另行设计异步 completion seam，不能在同步 hook 内 sleep。

语义以现有 Redis 命令和内嵌 Lua 为 golden contract，且必须自行复现 dispatcher 层的错误映射：

- 新记录 CAS 即使期望版本非零也允许写入，并返回版本 1；
- 已有记录只接受相等版本，成功后加一；冲突返回 `EN_DB_OLD_VERSION` 并把当前版本写回调用方（生产中该映射在
  `db_msg_dispatcher::dispatch` 完成，hook 绕过它，内存 backend 必须等价实现）；
- KL 索引永久单调递增，裁剪最小索引而不复用旧索引。
- 不带 CAS 的 KV set 和带 CAS 的成功 set 都只合并调用 message 中 present fields，不替换整个 hash；
- `inc_field` 遵循 `HINCRBY`，缺 key/field 时从零创建，字段 descriptor/numeric 校验仍在 hook 前完成；
- `get_by_indexs`/HMGET 保持请求顺序和一一对应的结果槽，不折叠缺失项；`update_by_index` 可 upsert 字段但不修改
  `index_number`；
- `remove_all` 同时清 record/list/index/TTL；空 remove-by-index 成功，而空 get-by-index 参数失败；
- `EXPIRE`/`PERSIST` 只修改已存在 key，但公共 API 仍按当前 `unpack_nothing` 的可观测语义返回成功，即使 Redis
  integer reply 为 0。

覆盖顺序：per-operation user rule -> per-table rule -> user backend -> 默认内存 backend。用户规则可选择
handled、继续默认或显式 passthrough；默认不允许真实 Redis passthrough。只有 fixture 显式初始化 DB
dispatcher 且提供真实配置时才允许 passthrough，否则立即返回“passthrough unavailable”，不得进入可能永不
完成的发送路径。

**per-table mock（每个表接口单独的 mock 接口）**。表身份无需扩展 hook ABI：生成 DB 层的 key 恒为
`{record_prefix}-{table_name}.{key_fields...}`，`table_name` 是 proto 消息名字面量（不含 `.`，见
`db_rpc_redis_kv/kl.cpp.mako` 的 `prefix_fmt_key` 与已生成代码），mock engine 在 handle 时剥离
`db_msg_dispatcher::me()->get_record_prefix()` 前缀并取到首个 `.` 即得表名；解析失败（非规范 key）视为
无表规则，回落默认后端。分两层：

- **引擎层表规则（无代码生成）**：`mock_db::mock_table(table_name)` 返回 RAII `db_table_rule_handle`
  （仿 `dns_rule_handle`，析构撤销；同表后注册规则优先）。规则选项：按 `op_type` 的错误注入
  （`set_error(op, code)`，表内其余 op 不受影响）、canned KV 记录（bytes + version，仅作用于
  get_all/partly_get/batch 组合）、强制 `EN_DB_RECORD_NOT_FOUND`、`set_passthrough(false)` 之外的
  `fallthrough`（规则放行时继续内存后端）。检查侧提供表级过滤统计 `calls(table_name, op)` 与
  `last_call(table_name)`，复用统一调用历史。
- **模板层 typed 接口（生成）**：见下段 `<db 命名空间>::mock`，把表规则的字符串表名与 bytes 包装成
  该表 descriptor 的 typed API。

typed 数据检查与行为注入接口（遵循 3.5/3.6）：`db_rpc_redis` 模板按表（index 命名空间）增量生成
`<db 命名空间>::mock` 子命名空间（宏门控纯新增，与 `<service>::mock` 同约定），**不传引擎、不引用工具库
类型**，经 `rpc::unit_test` 桥接访问绑定的 mock backend（raw entry：channel + 规范 key → bytes、CAS
version、TTL、KL 索引与条目），生成层持 `prefix_fmt_key`/`key_fields` 全信息，可按表 descriptor 精确重建
key 并编解码 typed message。每个表提供：

- key 构造：`make_key(key_fields...)`；
- 行为注入（返回 `rpc::unit_test::mock_rule_handle`，RAII 析构停用）：`set_error(op, code)`、
  `force_not_found()`；通用 canned/fallthrough 组合仍由引擎直连接口 `mock_db::mock_table` 提供；
- 数据准备（直写 backend，不经过 RPC、不要求 task 上下文）：KV `set_record(key_fields..., const
  table_xxx&, uint64_t version = 0)`、`set_ttl(key_fields..., seconds)`、KL `append_entry(const table_xxx&)`；
- 只读检查：KV `get`（含版本输出）/`has_record`/`version_equal`，KL `indexes()`/`entry_at(index)`/`count`。

### 8.6 UUID

在五类公共 UUID API 最前端增加统一 provider hook，默认 provider 每 fixture 重置：

- standard UUID、binary UUID 和 short UUID 生成格式合法且确定性的值；
- global increase id 按 `(major, minor, patch)` 维护独立单调 counter；
- global unique id 正数、单调，且因为入口被整体接管而不会触发生产静态号段缓存 `g_unique_id_pools`；
- 用户可替换整个 provider，或覆盖单个 UUID family。

provider ABI 不暴露协程返回类型：三个字符串入口（返回 `std::string`）使用同步 `try_generate_*`；global
increase/unique 两个原本返回 `rpc_result<int64_t>` 的入口由 provider 返回同步的 `handled + result code +
int64 output`，现有函数再用项目的 `RPC_RETURN_*` 包装，从而同时适配两种协程实现。

teardown 要清除测试 provider、恢复未安装 hook 时的生产路径并清理测试状态；测试需证明连续两个 fixture 从相同
seed 开始且不会被 production 静态缓存污染。

provider 的安装/覆盖 API 按 3.5 约定放在 `rpc::db::mock`（与 uuid 实现同级的 `mock` 子命名空间，宏门控）；
工具库只提供确定性默认 provider 并在 fixture start 时注册。

### 8.7 资源 hook

在 `excel_config_wrapper` 增加 scoped resource provider；首次 init 和后续 reload 都优先读取 active provider，
否则保持现有 filesystem loader（首次 init 会重设 loader 的既有行为不受影响，因为 provider 检查发生在回调
内部）。

内存 provider 支持：

- path -> bytes；
- path -> version；
- missing/read-error/version-error；
- 在测试中替换版本并触发 reload；
- 调用历史。

资源内容仍交给生成 config manager 解析并建立索引，不能用 mock accessor 绕过真实加载。本期一等支持范围明确为
Excel/config binary resource：提供通用 path→bytes/version provider，并由 excel wrapper 作为首个 adapter；其他
组件文件只有显式注册 adapter 后才属于覆盖范围，不 hook 全局 filesystem。

RAII 只安装/恢复 active provider registry。现有 wrapper 在首次 init 安装的 production loader/group wrapper 保持
进程生命周期，不能伪称 teardown 会恢复它们；工具不得调用不可移除的 `excel_add_on_group_loaded_callback` 注册
per-case listener。若确需测试 listener，须在 wrapper adapter 新增 hooks-on、token 可移除的窄监听 seam。生成 manager
模板不修改，fixture 配置中的资源用例需打开 `excel().enable()`。

### 8.8 其他 RPC/传输

实施时基于 2.9 的清单生成一份 inventory，按最终 transport 分类：

- rank、dtmq、distributed_transaction、user/auth 等使用 SS/DB 的组合 API：由对应基础 mock 覆盖，并各选一个
  消费者做契约测试。
- telemetry：只做观测，不作为 RPC transport mock；单元测试环境按 8.9 以纯配置保持 noop 或 file-only 导出。
- `src/component/orbit/sdk/**` 的 server/controller/agent 生成 RPC 及 server-to-client 封装已确认走
  `ss_msg_dispatcher`，由 SS mock 覆盖并选代表性消费者做契约测试。
- `GameSharedComponent/Orbit` 客户端 SDK 是当前唯一生产直连 atbus 例外。首版新增可选
  `${PROJECT_NAME}-rpc-unit-test-orbit` target：在 `OrbitClientRuntime::send_message(const std::string& packed_message,
  const google::protobuf::MethodDescriptor& method, bool reliable = false, uint64_t task_id = 0)` 的 endpoint 检查和
  `get_bus_node()->send_data` 前放 component-owned hooks-on seam，捕获 owned packed message/method/reliable/task-id，
  后续 pump 通过原 `on_received_message`/dispatcher（legacy `OrbitRPCDispatcher`）完成 pending callback、retry、error
  或 timeout；未处理时保持原 atbus fallback。adapter 不让 Orbit component 依赖测试库，registry 同样使用
  generation/导出边界。
- Orbit 客户端原生 API 是 callback/retry 模型，不是 server-frame coroutine result；其契约 case 仍从
  `rpc::async_invoke` 外层 task 发起，并用工具提供的 callback waiter/custom-resume bridge 带超时等待，不能只在
  `CASE_TEST` 栈上同步伪造 callback。
- HTTP/curl（HPA、etcd 工具、telemetry exporter）：列为组件扩展，不属于内置 RPC 核心；HPA prometheus pull
  有专用默认 hook（见 8.10）。

### 8.9 telemetry：纯配置离线，不引入 hook

已核实（`src/server_frame/rpc/telemetry/rpc_global_service.cpp`、`svr.telemetry.config.proto`，配置根
`logic.telemetry`）：

- exporter 启用条件因类型而异（不是统一的 `has_xxx() && 非空 endpoint`）：`otlp_grpc`/`otlp_http` 判
  `has_xxx() && !endpoint().empty()`，`otlp_file`/`prometheus_file` 判 `!file_pattern().empty()`，`ostream`
  是 proto `string` 字段（**无 `has_ostream()`**，仅判 `!ostream().empty()`），`prometheus_push` 需
  `has_prometheus_push() && host/port/jobname 三者非空`，`prometheus_pull` 判 `has_prometheus_pull() &&
  !url().empty()`。不配置任何 exporter 时 trace/metrics/logs 分别回退到进程默认 Noop provider，零网络。
- tracer 生命周期细节：仅在**从未调用 `set_current_service`（无 group）或进程关闭中**时 `tracer::start` 才
  返回 false；一旦 `set_current_service` 跑过（哪怕零 exporter），`default_tracer` 是合法的 Noop tracer，
  `start` 会成功并产出 noop span。两种情况 `rpc::context` 都用共享 noop span，均不崩溃。
- 纯本地导出已存在：`ostream`（stdout/stderr/文件）、`otlp_file`（含 `@stdout`/`@stderr` 特判）、
  `prometheus_file`。**trace provider 的真实条件是 `trace().has_exporters() && trace().has_processors()`**
  （不是"必须 simple"）：`_opentelemetry_create_trace_processor` 在 `has_simple() && !has_batch()` 时用
  Simple，否则默认 Batch，故 `processors.simple`、`processors.batch` 或空 `processors {}` 均可建 provider；
  只配 `exporters` 不配 `processors` 会回退 Noop。metrics 只需 `has_exporters()`，无 processors 要求。
- 初始化链 `logic_server_common_module::reload` → `rpc::context::set_current_service` →
  `global_service::set_current_service` 在 fixture 中走真实路径。

fixture 策略（回答“telemetry 是否可以仅本地文件输出绕过外部依赖”——可以，且纯配置可达，无需代码改动）：

1. 默认 fixture 配置不写 `logic.telemetry` exporter → 全 noop、零网络；
2. 需要验证 trace/metrics/logs 内容时，只配置 `otlp_file`/`prometheus_file`/`ostream`（trace 必须同时配
   `trace.exporters` 与 `trace.processors`，simple 或 batch 均可），输出目录指向 `<BUILD_DIR>` 下的 case
   工作目录；
3. 测试中禁止配置 `otlp_grpc`/`otlp_http`/`prometheus_push`（出站连接）和 `prometheus_pull`（入站监听
   端口）；runtime 对这几项做配置校验，发现即 fail-fast；
4. “noop 不崩溃”和“file-only 导出”都进自测矩阵；
5. 若未来需要断言 span 内容，可在 group 上挂内存 processor/exporter，属可选增强，不在 v1。

**补充（实施验证后）**：opentelemetry provider 链与命名 group 属进程生命周期状态，后续 fixture 的
`set_current_service` 不能可靠重建前一 fixture 已建立的 provider/exporter。凡 telemetry 配置相冲突的用例
（例如 HPA group 配置 vs otlp_file 导出配置 vs 空配置）必须拆到独立测试 executable（
`project_add_rpc_unit_test` 多 target），不能靠 fixture 顺序隔离；同一 executable 内首个带 telemetry 配置
的 fixture 决定全进程 exporter 行为。

### 8.10 HPA：默认关闭零网络；启用时默认安装 prometheus pull hook

已核实（`logic_hpa_controller.*`、`logic_hpa_policy.*`、`logic_hpa_puller_prometheus.*`、
`svr.hpa.config.proto`，配置根 `logic.hpa`）：

- HPA 对象由 `logic_server_common_module` 无条件创建并在 init 时建 curl share/multi 句柄，但**不发请求**；
  只有 `hpa.controller.enable && hpa.metrics.enable` 同时为真（默认均 false）才创建 policy/puller/discovery。
  因此默认 fixture 不需要任何 HPA mock 即零网络。
- pull 链路：`module::tick` → `logic_hpa_controller::tick` → `logic_hpa_policy::tick` →
  `logic_hpa_puller_prometheus::do_pull`（唯一经 `logic_hpa_controller::create_http_request` 发 HTTP 的点）
  → 响应 `logic_hpa_pull_result_prometheus::parse` → `logic_hpa_policy::trigger_event_on_pull_result`
  （公开 API）→ instant/range 回调与 ready。puller 的创建门槛是
  `metrics.exporters.has_prometheus_http_api()`（消息存在即创建，见 `logic_hpa_puller_factory::make_new_instance`），
  **不是 url 非空**；若配 `prometheus_http_api {}`（空 url）仍会创建一个惰性 puller，其 `pull_url_` 为空时
  `do_pull`/`can_pulling_available` 直接返回 false。要彻底不创建 puller，须完全不设 `prometheus_http_api`。

mock 设计（回答“是否可通过默认安装的 pull hook 让逻辑正常运行”——可以）：

1. seam：在 `logic_hpa_puller_prometheus::do_pull` 入口增加 hooks-on 分支（server_frame 实现代码内，宏
   门控）：已安装 hook 时记录 query（url、policy、时间窗）并**不创建** `http_request`，等待注入；
2. 注入 API 放在 HPA 实现同级的 `mock` 子命名空间（见 3.5）：测试提供预置 JSON，mock 构造
   `logic_hpa_pull_result_prometheus`（`parse(gsl::string_view) noexcept` 接受 Prometheus HTTP API 的 JSON
   响应体）后经公开的 `logic_hpa_policy::trigger_event_on_pull_result(logic_hpa_pull_result&)`（注意形参是
   **基类** `logic_hpa_pull_result&`，非 prometheus 专用类型）喂回，完整走通下游回调与 ready 状态，不绕过任何
   policy 内部逻辑；`do_pull()` 无参返回 bool，`create_http_request(gsl::string_view)` 返回
   `util::network::http_request::ptr_t`（存量老名，与 `atfw::util::network::http_request` 同类型）；
3. 默认行为遵循 3.4：`runtime_options` 启用 HPA feature 时 runtime **默认安装** pull hook；pull 属于“需要
   响应”的调用，未配置答案的 metrics 默认喂回错误结果并输出诊断。注意此处存在用户诉求 #2 与 #8 的交叉：默认
   hook 保证“逻辑正常运行”指的是**不触发真实 HTTP、pull 路径走完整 parse/trigger 回调链**；但由于
   `do_ready()` 仅在 `!result.is_error()` 时触发，只有测试显式提供**成功**指标答案后 policy 才会进入 `ready_`
   状态，默认错误结果不会置 ready（这正是提示补 mock 的信号）；
4. `logic_hpa_discovery` 的 etcd 写依赖真实 etcd 集群，v1 不提供 mock；需要 HPA 的测试保持
   `controller.configure_key` 为空（policy 的 `available_` 不依赖 configure_key，仅默认 discovery 不启用）；
5. 该 hook 属功能私有 mock，registry 归属 server_frame DLL，遵守 3.2 的 ownership 规则。

## 9. 生命周期与隔离

runtime 状态机：`constructed -> starting -> running -> stopping -> stopped`。

`start()` 顺序：

1. 检查进程中不存在另一个 active runtime；
2. 生成/装载最小测试配置：创建 atapp bus node 但默认**不配置 listen endpoint**，关闭 etcd、文件日志、外部
   exporter，设置短且非零的 stop timeout/interval，并按 `runtime_options.features` 物化
   `logic.server.shared_component`；resource feature 另打开 `excel().enable()`。先用 smoke gate 验证空 listen 配置，
   只有真实 feature 要求监听时才分配唯一的本地临时资源，禁止固定端口或伪造 `mem://` 地址；
3. 注册 `mock://` connector；
4. 安装 DB、UUID、DNS、CS、resource 等 scoped hook（HPA feature 启用时一并安装默认 pull hook）；
5. 添加公共模块和按 feature 请求的 dispatcher；
6. 执行用户 setup callback；
7. init app/modules（参照 libatapp 单测范式：`init(nullptr, argc, args, nullptr)` + 预热 `run_noblock`）；
8. 注入 discovery 节点并完成首轮 pump。

`stop()` 逆序执行：

1. 阻止新 mock 请求；
2. 校验 expected calls、未消费规则和待处理事件，先保存完整失败诊断；
3. 对仍存活 task 调用 `task_manager::kill_all` 并有界 drain；
4. 排空或丢弃待投递队列，移除 session、router manager/object 和 discovery 节点；
5. 调用 `app.stop()`，继续 pump 到 `app.is_closed()`，并设置独立 teardown hard deadline；超时则再次
   `kill_all`/触发 module timeout，报告失败，不能直接依赖 app 析构的默认长等待；
6. cleanup/释放 app modules，确认 logic common singleton、task manager native state 等进程级引用已复位；
7. 清除所有 scoped hook/provider（不改动 excel manager 的进程生命周期 production wrapper）；
8. 清理 DB、UUID、resource、调用历史和单例引用；
9. 释放 active-runtime guard。

hook registry 使用 shared state 和 generation token；server frame 不保存指向 fixture 成员的裸指针。测试可执行
文件内部 case 按私有框架的现有顺序串行运行；跨 executable 可由 CTest 并行。工具自身必须连续启动/停止至少两个
fixture，验证 app/global module/task/resource provider 均未泄漏；hard-timeout 后的 poisoned runtime 只允许 stop，
不允许再次 run_task/wait。

## 10. 工具自身测试

### 10.1 必需组合 smoke case

一个 `CASE_TEST(rpc_unit_test, db_dns_ss_complete)` 必须在同一个 `rpc::async_invoke` task 中完成：

1. 启动含 SS/DNS/DB 的最小 runtime；
2. 向 atapp global discovery 注入 `mock://` 远端节点；
3. mock 并调用基础内置 unary SS RPC `${PROJECT_NAMESPACE}.RouterService/router_transfer`，断言请求内容/目标及真实
   suspend/resume；名称中的 Router 不代表 `router_rpc: true`，它当前是普通 unary；
4. mock `rpc-unit-test.invalid`，返回 RFC 保留地址 `192.0.2.1` 与 `2001:db8::1`；
5. 通过已存在的 `rpc::db::login_auth::replace/get_all` 操作 `PROJECT_NAMESPACE_ID::table_login_auth`，断言 `open_id`、`user_id`、
   `access_token_code` 与 CAS version；该 API 定义在 `local_db_interface.atfw.gen.*`，由
   `svr.local.table.proto` 生成；
6. task 等待 SS、DNS 和 DB 完成后返回；外层 `runtime::wait` 使用更大的 hard timeout；
7. 断言 task 正常退出、SS/DNS/DB 调用次数正确、无未消费规则、无存活 task、无真实 I/O。
8. 显式调用并断言 `runtime::stop()`，确认 app 关闭和所有 scoped state 清理后 case 才退出。

已核实的内置 RPC 候选（`src/server_frame/protocol/private/protocol/pbdesc/svr.protocol.proto`，
package 由 `PROJECT_NAMESPACE` 决定）：

- `${PROJECT_NAMESPACE}.LogicCommonService/set_server_time`：**存在**，请求侧 stream + `allow_no_wait: true` +
  `enable_broadcast: true`，适合验证 SS 发送链与 broadcast 计数，但不能验证响应唤醒。**注意**：生成器在
  stream 模式下强制 `rpc_allow_no_wait = False`（`rpc_call_api_for_ss.cpp.mako` 的 `if not rpc_is_stream_mode:`
  分支），所以 `set_server_time` 只能证明 stream/broadcast 分支，**不能**证明非-stream 的 `allow_no_wait`/
  `wait_later` 分支；后者需靠 test-only proto 的显式非-stream 方法覆盖；
- `${PROJECT_NAMESPACE}.RouterService/router_transfer`：**已确认是普通 unary**（`SSRouterTransferReq` →
  `SSRouterTransferRsp`），没有 `router_rpc: true`，用于内置 unary 响应唤醒证明；
- `${PROJECT_NAMESPACE}.RankBoardService/rank_get_top` 等 unary SS RPC 存在于 `src/component/rank` 的协议中，但不在
  server_frame 协议链接范围内，仅在工具自测选择链接组件协议时可用。

另有必需的 test-only proto（`test/protocol/rpc_unit_test.proto`）通过现有 RPC 生成流程构建，用来实例化当前仓库
没有启用实例的 `router_rpc: true` 及其他缺失生成组合；它不替代上述“基础内置 SS”验收，生成物不手改。

### 10.2 完整自测矩阵

| 范围 | 必测行为 |
| --- | --- |
| Runtime | start/stop、空 listen、重复 fixture、并发 runtime 拒绝、task 成功/fault/cancel/task-timeout、hard-timeout poison/kill-all、teardown deadline、泄漏清理 |
| Discovery/transport | id、name、discovery-node、type/zone、metadata、add/update/delete、consistent-hash/random/round-robin、broadcast、多节点、payload ownership、send-ack |
| SS | unary、stream、no-wait/wait-later、user、broadcast、RPC/transport error、malformed、delay、timeout、两类默认行为（单向成功丢弃/需响应报错）及覆盖 |
| Router | test-only router/unary、router/stream（经 test manager + object）、manager-not-found fallback、type-id 冲突/lifetime |
| DNS | A、AAAA、多记录、空结果、error、乱序、delay、timeout、未匹配报错、cancel 后 late response |
| CS | add/post/remove/set-router-rsp、typed request、downstream data、kickoff、set-router、broadcast、send error、多客户端 |
| DB KV | empty、presence-aware set/get/partial/batch、CAS 成功/冲突（含版本回写）、inc、remove、TTL |
| DB KL | add/index 单调、max-length 裁剪、ordered missing slots、get/update/upsert/remove、空列表、TTL |
| UUID | 五类入口、格式、单调性、seed/reset、全局/per-family override |
| Resource | 首次 init、reload/version、missing/error、索引可见、provider generation 清除、连续 fixture 不泄漏 |
| Orbit adapter | 两套 client SDK 请求入口、stream、callback response、send error、retry、timeout、late response、fallback；由外层 task 等待 |
| Telemetry | 空配置 noop 零网络、file-only 导出可断言、未初始化 tracer 安全、禁止项 fail-fast |
| HPA | 默认零网络、启用时默认安装 pull hook、预置指标经 parse+回调完整注入、未配置答案报错、configure_key 为空时 discovery 不启用 |
| Hooks OFF | server frame 无测试 registry/分支/`mock` 符号，生产 fallback 可构建 |

### 10.3 双协程构建

同一套测试源码必须分别构建：

- `-DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=ON`
- `-DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=OFF`

不得在测试 case 内使用 `#if` 分出两套调用方式。差异只存在于框架现有的 return type、`RPC_AWAIT_*` 宏和
`task_type_trait` 实现中。

## 11. 用户文档计划

`src/tools/rpc-unit-test/README.md` 至少包含：

- 适用范围、非目标、hook 构建开关和生产开销说明；
- 最小 CMake 示例及全部参数；
- 最小 `CASE_TEST` + `runtime` + `run_task/wait` 示例；
- discovery 节点、SS/router/DNS/CS/DB/UUID/resource 的常用注册示例；
- atapp raw transport 的 id/name/discovery/hash/random/round-robin 规则与历史断言示例；
- C++20 与传统协程共用写法；
- 默认严格策略、DB/UUID 默认行为和用户覆盖优先级；
- 单向通知/需响应 RPC 的两类默认行为规则（3.4）与 per-rule 覆盖方式；
- `mock` 子命名空间与代码分布约定（3.5）：工具库公共 API、SS/DB 生成 mock 接口、HPA/UUID 功能私有 mock 的
  各自位置；
- 外部依赖离线配置矩阵（2.10）与禁止配置项（etcd、bus.proxy、主机名 listen、OTLP/push/pull exporter、固定
  端口）；
- telemetry 的 noop/file-only 用法（8.9）与 HPA pull 指标注入（8.10）；
- timeout 四层关系：RPC timeout、task timeout、runtime hard timeout、CTest timeout；hard timeout 会 poison 当前
  runtime 并 kill-all，不能继续复用；
- fixture 生命周期、单进程串行限制和 teardown 要求；
- Windows 下运行测试可执行文件的 DLL/PATH 要求（引用 `.agents/skills/testing/SKILL.md` 的既有方案）；
- 常见失败诊断，例如未匹配 RPC、response sequence 错误、task 未进入 wait、残留 session/task；
- 组件私有 transport adapter 的接入方式；
- Orbit client adapter 的可选 target、callback waiter、retry/timeout 与生产 fallback；
- 构建、按 label 运行和过滤单 case 的命令。

文档示例必须由工具自身测试复用或编译，避免 README 与实际 API 漂移。

## 12. 分阶段实施清单

### 阶段 0：建立契约和 build feature

- [ ] 在工程构建选项中新增 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`（`cmake_dependent_option`），在
  `server_frame_build_feature.h.in` 增加 `#cmakedefine01` 项；默认值与
  `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` 对齐。
- [ ] 确认 OFF 构建完全裁剪 hook，并增加 compile-only 验证。
- [ ] 按 owning target 分别放置唯一 registry：核心在 `server_frame`、资源在 `server_frame-config`、Orbit 在 SDK；
  为 DLL 边界规定 export、ownership、generation 和恢复协议，且不改变既有公开 class 布局或反转链接依赖。
- [ ] 基于 2.9 建立 RPC/transport inventory，记录每个生成 API 的最终 seam。

完成条件：评审通过 seam 表，生产 OFF target 不依赖测试库。

### 阶段 1：CMake helper、私有测试框架和 runtime

- [ ] 在 `src/CMakeLists.txt` 的 `component` 之前按 `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` 门控添加
  `src/tools/rpc-unit-test`，创建库 target 和 alias。
- [ ] 实现 `project_add_rpc_unit_test`，复用集中编译的 atframe_utils 私有 main/frame support targets。
- [ ] 自动 `add_test`、labels、CTest timeout、`ENVIRONMENT_MODIFICATION` PATH、运行时依赖和既有 post-build
  helper。
- [ ] 实现 runtime 状态机、最小 app/common module、setup callback、task handle 和 wait pump（含
  `time_utility::update()`）。
- [ ] 覆盖 success/fault/cancel/task-timeout/hard-timeout poison+kill-all/teardown deadline/连续 fixture。

完成条件：一个空的 async task 能在两种协程模式下由普通 `CASE_TEST` 带超时等待完成。

### 阶段 2：atapp discovery 与 SS

- [ ] 实现 `mock://` connector（含 `get_address_type`、fake connection handle），使用 app global discovery
  注入节点。
- [ ] 完成独立 raw transport rule/history、owned 异步队列、一代 pump barrier、正确 sender/ack 和严格诊断；覆盖
  id/name/discovery-node/consistent-hash/random/round-robin/metadata/broadcast。
- [ ] 在 raw transport 上完成 typed SS rule engine、真实 dispatcher 回注及 stream/unary/no-wait/wait-later。
- [ ] 实现 `router_test_manager` + `router_test_object`（同步寻址到 mock 节点），完成 router/unary、router/stream 与
  manager-not-found 契约测试；不修改 Mako。
- [ ] 用内置普通 unary `${PROJECT_NAMESPACE}.RouterService/router_transfer` 完成响应唤醒证明。
- [ ] 必须生成 test-only proto，覆盖当前仓库未实例化的 `router_rpc: true` 和其他缺失 Mako 组合；生成物只在
  build tree。
- [ ] 完成内置 SS stream smoke（`${PROJECT_NAMESPACE}.LogicCommonService/set_server_time`）。
- [ ] 落实 3.4 两类默认行为：单向通知类默认记录并成功，需响应类默认快速失败，且 per-rule 可覆盖。
- [ ] 在 `rpc_call_api_for_ss.*.mako` 增量生成 `<service>::mock` typed 注册辅助（宏门控纯新增），经现有流程
  重新生成并双模式编译；test-only proto 同步获得生成 mock 接口。

完成条件：普通 unary 在 task 中 suspend/resume；router 和 broadcast 有独立契约测试；未匹配调用按 3.4 默认行为
处理（需响应类快速失败，单向通知类记录并成功）。

### 阶段 3：DNS

- [ ] 在 `send_dns_lookup` 发起 `uv_getaddrinfo` 前增加 hook 和 scoped registry。
- [ ] 用原 message type/sequence/custom-resume 完成异步响应。
- [ ] 覆盖 A/AAAA/error/delay/乱序/timeout/unmatched。

完成条件：无系统 DNS 请求，`dns::lookup` 的真实 wait 路径在 task 中完成。

### 阶段 4：DB 与 UUID

- [x] 在所有 `hash_table` 公共操作接入统一同步 hook，保持 task/descriptor/字段/空参数校验与 tracing。
- [x] 为 hooks-on 增加不启动 I/O 的 dispatcher test identity（既有 prefix/channel 字段），默认设为
  `unit-test`/`RAW_DEFAULT`，检测已初始化 dispatcher 并在 teardown 恢复。
  （实现为 `db_msg_dispatcher::set_record_info_for_unit_test`：存在活动连接或不同 prefix 时拒绝；
  teardown 不恢复 prefix，同值重复设置幂等，避免跨 fixture 单进程冲突。）
- [x] 实现 presence-aware KV/KL/TTL/CAS 内存 backend（含 field merge、ordered missing slots、TTL public
  semantics、`EN_DB_OLD_VERSION` + 版本回写）和可控时钟。
  （`mock_db`：存序列化字节保 presence，partly_get 清理未请求字段，KL 单调索引/trim，TTL 惰性过期。）
- [ ] 以当前 Redis 命令和两段内嵌 Lua 建 golden tests。
- [x] 在五类 UUID 公共入口接入 provider，完成确定性默认实现和覆盖 API。
  （偏离：`uuid_allocator::inc_field_auto_inc_id` 经由 `hash_table::key_value::inc_field`，已被 DB hook
  覆盖，无需 uuid.cpp provider seam；`generate_global_unique_id` 由 mock_db 确定性服务。注意
  `g_unique_id_pools` 静态号段缓存在单进程跨 case 不清零，号段基数会延续，测试不得断言绝对 id 值。）
- [x] 证明默认 fixture 不初始化 Redis dispatcher、不创建网络连接、无跨 case 静态状态泄漏（含
  `g_unique_id_pools`）。
- [x] 引擎层 per-table 规则（见 8.5）：`mock_db::mock_table(table_name)` RAII 表规则（按 op 错误注入、
  canned 记录、强制 NOT_FOUND、fallthrough）、表名从规范 key 提取（剥离 record prefix 取首个 `.` 前段），
  表级 `calls(table, op)`/`last_call(table)` 统计；未匹配表回落内存后端。
- [x] raw entry 访问 API：`mock_db` 暴露 channel + 规范 key 直读写（descriptor/type 名、owned bytes、
  CAS version、TTL、KL 索引与条目），供模板层 typed 适配与断言使用，不经过 RPC、不要求 task 上下文。
- [x] 模板层 `<db 命名空间>::mock` typed per-table 接口（`db_rpc_redis` 宏门控增量生成，经
  `rpc::unit_test` 桥接访问引擎）：`make_key`、`set_error`/`force_not_found`（RAII
  `mock_rule_handle`）、数据准备（`set_record`/`set_ttl`/KL `append_entry`）、只读检查（`get`/
  `has_record`/`version_equal`/KL `indexes`/`entry_at`/`count`）；smoke case 见
  `db_login_auth_typed_mock_table_interface`。
- [x] 自测明确调用 `rpc::db::login_auth::replace/get_all` 与 `PROJECT_NAMESPACE_ID::table_login_auth`，不使用仅测试 backend 的
  私有捷径；并用 `rpc::db::login_auth::mock` 覆盖单表错误注入与 typed 断言。

完成条件：完整 DB 操作矩阵通过，CAS/列表与当前 Lua 一致，用户能覆盖单操作或整个 backend/provider。

### 阶段 5：CS

- [x] 收敛 CS dispatcher 的统一 gateway-send 边界并加入可选 hook；携带 `session_ids` 供观测，但生产 fallback
  保持当前行为，不在本变更顺带修复语义。
  （实现为私有 `send_serialized_to_gateway` + 类内 `unit_test_gateway_send_request`/static
  `set/get_gateway_send_hook_for_unit_test`；四分支全部改经此边界。）
- [x] 实现 mock client 的 add/post/remove/set-router-rsp 与 gateway 线格式 typed pack/unpack。
  （`mock_cs`/`mock_client`：上游经真实 `dispatch` 注入，下游 hook 捕获解析为 typed record。）
- [x] 覆盖 downstream、kickoff、set-router、broadcast、多客户端和错误。
  （4 个 case：生命周期+未知 RPC 错误响应、下行捕获三操作、广播+多会话观测、错误注入+无会话 kickoff。
  跨 fixture 修复：atapp stop 会 disable 单例 module，runtime 增加 `reenable_for_unit_test` 重新启用。）
- [x] 审计 CS Mako 所有分支均经过该边界；若需修改则统一重新生成。
  （审计结果：`session_downstream_api_for_cs`/`handle_cs_rpc` 经 session → cs_msg_dispatcher → 统一边界，
  无绕过，模板未改。）

完成条件：真实 CS action task 可由上行请求启动，下行消息无需 atbus 即可断言。

### 阶段 6：server_frame 组件单元测试 target

- [ ] 选址 `src/server_frame/test/` 放置 server_frame 组件的单元测试 case 代码（与 vendored 子项目
  `libatbus/test`、`libatapp/test` 的"测试贴近被测组件"惯例一致）。框架自测保留在
  `src/tools/rpc-unit-test/test/`：自测验证工具库自身契约，组件测试验证 server_frame 行为
  （dispatchers、生成 RPC/DB API、session_manager、router、uuid 等）；两者共用同一私有测试框架与
  `project_add_rpc_unit_test` helper，互不引用 case 代码。
- [ ] `server_frame/CMakeLists.txt` 的 GLOB_RECURSE 跳过 `test/` 子目录（沿用 config/protocol 的
  skip-glob 机制）；`src/CMakeLists.txt` 在 hooks 条件块内追加 `add_subdirectory(server_frame/test)`，
  hooks-off 构建中不产生任何相关 target。
- [ ] target `${PROJECT_NAME}-server-frame-unit-test`（COMPONENT `server-frame`，LABELS fast，链接
  `${PROJECT_NAME}::rpc-unit-test` 与 `${PROJECT_SERVER_FRAME_LIB_LINK}`，经 `ctest -L "component:server-frame"`
  独立可筛选）。
- [ ] 初始用例：`rpc::db::login_auth` 生成 API 全流程（replace/get_all/CAS 冲突
  `EN_DB_OLD_VERSION` + 版本回写/remove_all，与 `<table>::mock` per-table 规则组合）；
  `rpc::db::uuid_allocator::inc_field_auto_inc_id` 按 `(major, minor, path)` 独立单调递增；
  CS session 生命周期（`mock_client` add 经真实 `session_manager` 建 session、remove 触发 kickoff
  下行捕获、注入错误与多客户端）。
- [ ] 后续把 rank/dtmq/distributed_transaction/user/auth 等组件级用例放入各自 component 的 `test/`
  子目录并复用同一 helper（见阶段 8 的抽样契约测试）。

完成条件：`ctest -L "component:server-frame"` 独立通过；hooks-off 构建中 `src/server_frame/test/` 不产生
任何 target 且不污染 server_frame 库源码列表。

### 阶段 7：资源 provider

- [x] 在 excel wrapper 添加 scoped provider seam，避免首次 init 覆盖测试 loader。（
      `excel_config_wrapper.h/.cpp` 的 `set/clear_excel_resource_provider_for_unit_test`，buffer/version 回调内
      优先查询 active provider，否则走既有 filesystem loader；生产 loader/group wrapper 仍在首次 init 安装且
      保持进程生命周期，teardown 只清 active provider。）
- [x] 实现内存 bytes/version/error 和 reload。（`mock_resource`：path→bytes、version、version_error、
      调用历史；`reload()` 经真实 `excel_config_wrapper_reload_all(false)` 驱动完整 reload 流程。）
- [x] 验证真实 manager parse/index/group wrapper；teardown 只清 active provider，不注册不可移除的 per-case
      callback，也不伪装能恢复进程生命周期 loader。（`server_frame_test_resource.cpp`：真实 manager 解析
      `xresloader_datablocks` bytes 并建索引、版本替换 + reload、同版本 no-op、缺文件 reload 失败、fixture
      间 provider 隔离；manager 单例为进程生命周期，各 fixture 使用不同版本号。）

完成条件：同一 case 可替换版本并 reload；下一个 fixture 看不到旧 provider bytes/version 或测试 listener。

### 阶段 8：其他 transport、整合自测和文档

- [ ] 完成 rank/dtmq/distributed_transaction/user/auth 及 `src/component/orbit/sdk/**` 的 SS/DB 消费者抽样契约测试。
- [ ] 实现独立 Orbit client adapter target、component-owned seam 和 callback waiter；覆盖两套请求入口、
  callback/retry/error/timeout/fallback，且从真实 `rpc::async_invoke` task 发起。
- [ ] 实现必需的 DB + DNS + SS 组合 smoke case。
- [x] 实现 HPA `do_pull` seam 与 `mock` 子命名空间注入 API；runtime 启用 HPA feature 时默认安装 hook，验证无
      etcd 配置下 policy 回调链路完整、未配置答案报错。（server_frame `logic/hpa/mock/`：do_pull 入口 hooks-on
      分支记录 query 不发 HTTP，answer 经真实 `trigger_event_on_pull_result` 链喂回；工具 `mock_hpa` 按
      metrics_name 配置答案，未配置默认喂 error 且不置 ready。注意 policy 仅在存在激活的 instant/range 回调时
      才真正 pull——`is_pulling_available` 语义，测试需先注册回调。）
- [x] 验证 telemetry 空配置 noop 零网络、file-only 导出与禁止项 fail-fast。（`server_frame_test_telemetry.cpp`
      三案；otlp_file 需小 flush 参数。进程生命周期限制见 8.9 补充：telemetry 相冲突配置的用例必须独占
      executable。）
- [ ] 编写 README，并让示例参与编译。
- [ ] 在 Debug hooks-on、Release hooks-on、production hooks-off、两种协程、Windows/Linux 可用环境执行矩阵；
  公开接口示例另以 C++17 编译约束验证。

完成条件：本文第 14 节全部验收条件满足，CTest label 可被组件和 CI 直接使用。

## 13. 验证命令计划

实施时先沿用工作区现有构建树和生成器：

```powershell
cmake --build build_jobs_cmake_tools --target <rpc-unit-test-self-test-target> --parallel 12
ctest --test-dir build_jobs_cmake_tools -L rpc-unit-test --output-on-failure
```

Windows 的 CTest 用例由 helper 通过 `ENVIRONMENT_MODIFICATION` 自动补 PATH；只有直接运行测试 executable 时，
才按 `.agents/skills/testing/SKILL.md` 将 `<BUILD_DIR>\publish\bin\<Config>` 与
`third_party\install\windows-amd64-msvc-19\bin` 注入 PATH。

注意：本工作区 Windows 构建树的 ninja 头文件依赖追踪失效（本地化 cl.exe `/showIncludes` 前缀与
`msvc_deps_prefix` 不匹配，`ninja -t deps <obj>` 显示 `#deps 0`），**修改头文件后增量构建不会重编译**，必须
先 `ninja -t clean <受影响 target>` 再构建，否则新旧 ABI 混链会在运行期崩溃。详见
`.agents/skills/build/SKILL.md` 的 Header-change incremental-build pitfall 一节。

第二协程模式使用独立 build tree，不能覆盖当前配置；路径放在已解析 build dir 下：

```powershell
cmake -S . -B build_jobs_cmake_tools/_agent_tmp/rpc-unit-test-legacy `
  -G Ninja `
  -DPROJECT_ENABLE_UNITTEST=ON `
  -DPROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS=ON `
  -DPROJECT_SERVER_FRAME_USE_STD_COROUTINE=OFF
cmake --build build_jobs_cmake_tools/_agent_tmp/rpc-unit-test-legacy --parallel 12
ctest --test-dir build_jobs_cmake_tools/_agent_tmp/rpc-unit-test-legacy -L rpc-unit-test --output-on-failure
```

另建 hooks-off 配置，只做 server-frame/代表性服务的编译与符号检查，确认生产 target 没有测试 registry 和测试
库依赖。最终命令需补齐当前 `.vscode/settings.json` 的全部 configure 参数（`CMAKE_CXX_STANDARD=20`、
`CMAKE_BUILD_TYPE=Debug`、`PROJECT_ENABLE_SAMPLE`、`PROJECT_ENABLE_TOOLS`、
`ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_CRYPTO_USE_OPENSSL=ON` 等），而不是仅复制以上关键项。

格式与静态检查按受影响文件执行：C++ 用 clang-format/cpplint/clang-tidy 的工程规则，CMake 用 cmake-format/
cmake-lint，Markdown 用 markdownlint；生成文件通过生成命令验证，不手工格式化。

## 14. 最终验收条件

- [ ] `src/tools/rpc-unit-test` 提供独立支持库、CMake helper、自测和 README。
- [ ] 所有测试 executable 只使用 atframe_utils 私有框架；即使工作区存在 GTest target 也不会切换。
- [ ] 任意组件能用一次 CMake 调用创建 target、注册 CTest、labels 和 timeout。
- [ ] 普通 `CASE_TEST` 能启动真实 `rpc::async_invoke` task，并带 hard timeout 等待完成；hard timeout 会 kill-all、
  poison runtime 并受限时 teardown。
- [ ] 同一套测试源码通过标准协程 ON/OFF 两种构建。
- [ ] discovery 在 atapp global discovery 注入，SS 通过 `mock://` connector；不在 `logic_server_setup` 注入
  伪数据。
- [ ] raw transport 覆盖 atapp 的 id/name/discovery/hash/random/round-robin 数据发送选择、owned payload/metadata、
  send-ack 与入站 sender；SS mock 建立在它之上。
- [ ] SS unary/stream/no-wait/wait-later/broadcast/user 均有 mock 和契约测试；router RPC 经
  `router_test_manager` + `router_test_object` + mock connector 覆盖，Mako 不加入 mock 分支。
- [ ] DNS 通过原 custom-resume 机制完成，测试过程无系统 DNS 请求。
- [ ] CS 上行走真实 dispatch/action（gateway 线格式封装），下行 data/kick/set-router/broadcast 均可捕获且无
  atbus。
- [ ] 默认 DB 是完整 presence-aware 内存实现，field merge/CAS/KL/TTL 语义与当前 Redis/Lua 和公共 API 一致
  （含 `EN_DB_OLD_VERSION`、版本回写和 ordered missing slots），所有操作可覆盖；v1 不虚构 DB latency。
- [ ] 五类 UUID 默认被确定性 provider 接管，且用户可覆盖、case 间不泄漏。
- [ ] Excel/config 资源通过 wrapper provider 进入真实 manager reload/index；provider generation 可可靠清除，
  不注册不可移除的 per-case callback。
- [ ] `src/component/orbit/sdk/**` 的 RPC 由 SS mock 契约证明；`GameSharedComponent/Orbit` 直连 atbus 由独立可选
  adapter 覆盖 callback/retry/timeout，外层调用仍在真实 task 中。
- [ ] 必需的 DB + DNS + 基础内置 SS 组合 case 在一个 task 中完成并由外层等待后退出。
- [ ] 网络 mock 未匹配时快速失败并输出调用与规则诊断（单向通知类除外：默认记录并成功，见 3.4）；timeout case
  有 RPC、task、runtime hard、CTest 四层保护。
- [ ] 工具目录只含公共代码；service/功能 mock 接口位于各自实现/生成代码的 `mock` 子命名空间并被宏整体门控。
- [ ] DB 引擎层 per-table 规则与模板层 `<db 命名空间>::mock` typed 接口，可在测试中对单表注入错误/
  canned 数据并断言记录内容、版本、TTL 与 KL 索引（见 8.5）。
- [ ] telemetry 在 fixture 默认配置下零网络（noop），file-only 导出可选，禁止项 fail-fast。
- [ ] HPA 默认零网络；测试启用时默认安装 pull hook，预置指标经公开回调完整注入，未配置答案报错。
- [ ] hooks-off 的生产构建没有测试状态、热路径分支、`mock` 符号或测试库依赖。
- [ ] test-only proto 通过现有 helper 生成到 build tree，覆盖当前未实例化的生成分支；不手改生成输出。若 CS 审计
  确需 Mako 变更，必须经过统一重新生成和双模式编译验证。

## 15. 风险与实施 gate

以下不是留给实现者猜测的开放设计，而是进入相应阶段前必须以源码/测试关闭的 gate：

| Gate | 关闭方式 | 未关闭时禁止事项 |
| --- | --- | --- |
| test manager/object 能同步把 key 寻址到 mock 节点且不触发真实 cache 拉取 | 补齐 manager/object 全部纯虚接口、校验 type-id 无冲突和 object lifetime，跑 test-only router unary/stream 契约测试 | 禁止修改生产 Mako 或声称 router 已覆盖 |
| test-only proto 的生成覆盖可重复 | 用现有 helper 实例化 `router_rpc: true` 等缺失组合，检查 build-tree 输出、依赖边与二次构建无无效重生成 | 禁止提交/手改生成输出或用普通 `router_transfer` 冒充 router 分支 |
| CS 所有下行路径汇聚到单一 send seam | 枚举 dispatcher 和 CS Mako 的 data/kick/router/broadcast 分支 | 禁止只 hook `send_data` 后声称 CS 全覆盖 |
| DB 内存语义与 Redis 一致 | 以现有命令和两段内嵌 Lua 建 golden tests，含 CAS 首写、版本回写和 KL 裁剪 | 禁止凭常见 Redis 习惯实现版本/索引 |
| 默认 DB 身份不触发 I/O | hooks-on 仅设置既有 prefix/channel，断言 timer/连接未初始化且 teardown 恢复；已初始化实例默认拒绝覆盖 | 禁止为获得有效 channel 调用 `db_msg_dispatcher::init()` |
| resource provider 隔离 | 连续两个 fixture 做 init/reload/teardown，检查 active provider generation 已清除；production loader/group wrapper 保持进程生命周期 | 禁止保存 fixture 裸指针、注册不可移除的 per-case callback 或承诺恢复 manager loader |
| Orbit client seam 覆盖两套入口 | 枚举 `OrbitClientRuntime` method-descriptor API 与 `OrbitRPCDispatcher` legacy API，验证它们在 pending 注册后汇聚 send seam，并由原 receive/timeout/retry 路径完成 | 禁止用 SS mock 冒充 direct-atbus 覆盖或同步直接调用 callback |
| HPA pull hook 不改生产路径 | hooks-off 编译；hook 未安装时 `do_pull` 走真实 `create_http_request` 的契约审查；注入必须经 `parse` + `trigger_event_on_pull_result` | 禁止在 hook 内直接篡改 policy 结果或绕过 parse/回调 |
| 生成 mock 接口纯增量且宏门控 | 模板 diff 审查；hooks-off 双模式编译产物无 `mock` 符号；hooks-on 与工具库链接通过 | 禁止手改生成物或在既有调用热路径加分支 |
| private test framework 不切 GTest | 在存在 GTest target 的配置中检查 link line 和运行输出 | 禁止复用会自动选框架的 helper |
| hook 生产零开销 | hooks-off 编译、符号/依赖检查，审查预处理后的热路径 | 禁止仅依赖 `NDEBUG` |
| DLL 边界每个 seam 只有一份状态 | Windows shared 构建分别跨 server-frame、server-frame-config、Orbit SDK 与 test DLL 安装/读取同一 generation，检查导出符号、依赖方向和 OFF 布局 | 禁止 header-local static/inline registry、跨 DLL 复制或 production target 依赖测试库 |

实施过程中若发现现有 API 无法满足某个 gate，先更新本计划中的 seam、受影响文件和验收测试并评审，再写扩大的
实现；不得以 bypass dispatcher、直接完成 task 或降低断言覆盖来规避。

## 16. 已核实 seam 与 API 索引（源码引用）

下表为本计划在评审阶段逐条对照仓库源码核实的关键 seam。行号会随代码变动漂移，故以稳定的符号/函数名为准；
实施时应重新定位符号确认。

### 16.1 测试框架与 CMake

| 事项 | 位置 | 说明 |
| --- | --- | --- |
| 私有测试 main | `atframework/atframe_utils/test/app/main.cpp` | 无框架宏时走 `run_tests(argc, argv)` |
| `CASE_TEST` / 断言 | `atframe_utils/test/frame/test_macros.h`、`test_case_base.cpp`、`test_manager.cpp` | 静态对象自动注册；CLI `-l/-f/-r` |
| helper 的 GTest 分支 | `atframe_utils/test/test.build_bin.cmake`（`atframe_add_test_executable`） | 条件 `if(TARGET GTest::gtest OR GTest::GTest)` 定义 `PROJECT_TEST_MACRO_ENABLE_GTEST=1`；框架源码实际判断 `UTILS_TEST_MACRO_TEST_ENABLE_GTEST`，二者宏名不一致——本工具不依赖该缺陷并规避 helper |
| 手工链接范例 | `src/component/test/ItemAlgorithmTest/CMakeLists.txt` | 直接编译 frame 源码 + include frame 目录；无 `add_test`，且不受 `PROJECT_ENABLE_UNITTEST` 门控 |
| 子目录顺序 | `src/CMakeLists.txt` | `server_frame` → `component` → 各 svr → `tools` → `robot`；函数须先于组件定义 |
| CTest 门控 | 根 `CMakeLists.txt` | 仅 `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` 时 `include(CTest)` |
| 根工程/导出命名 | 根 `CMakeLists.txt`、安装 export | `PROJECT_NAME=atf4g-co`，导出 namespace 为 `${PROJECT_NAME}::` |
| server frame 链接变量 | `src/server_frame/server_frame.cmake` | `PROJECT_SERVER_FRAME_LIB_LINK = server_frame` |
| build feature 生成 | `src/server_frame/config/include/config/server_frame_build_feature.h.in` + `generate_config_codes.cmake` | `configure_file(... @ONLY)` 展开 `#cmakedefine01`；新增 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` 于此 |
| 协程开关 | `project/cmake/ProjectBuildOption.cmake` | `PROJECT_SERVER_FRAME_USE_STD_COROUTINE`（`cmake_dependent_option`）；`PROJECT_ENABLE_UNITTEST` 默认 OFF |
| Windows DLL 运行环境 | `.agents/skills/testing/SKILL.md`；`project/cmake/ProjectTools.cmake`；`ItemAlgorithmTest/CMakeLists.txt` | 复用 executable 的 bash/pwsh post-build 变量；CTest 用 `ENVIRONMENT_MODIFICATION` 追加 PATH，直接执行由用户环境提供 PATH |

### 16.2 atapp 服务发现与传输

| 事项 | 位置 | 说明 |
| --- | --- | --- |
| 注入 discovery 节点 | `app.get_service_discovery_module()->get_global_discovery()`（`libatapp/include/atframe/modules/service_discovery_module.h`，返回非 const `etcd_discovery_set&`；`app` 自身只有 const 重载） | `add_node(ptr_t)` / `remove_node(...)`；`make_strong_rc<etcd_discovery_node>()` + `copy_from(info, version, 0)` |
| 注册 mock connector | `app::add_connector<TCONNECTOR>(args...)`（`atapp.h` 公开模板） | 基类 `atapp_connector_impl`；`register_protocol` 为 protected；`get_address_type` 纯虚；参照内置 `atapp_connector_loopback` |
| 捕获出站 | `atapp_connector_impl::on_send_forward_request(handle, type, uint64_t* msg_sequence, data, metadata)` | mock connector override 此虚函数入队 |
| atapp 选择发送 | `app::send_message*`、`send_message_by_consistent_hash`、random/round-robin/discovery-set 重载 | 最终进入 endpoint/connector；由独立 raw transport mock 验证，不限于 SS |
| 注入入站回包 | `app::trigger_event_on_forward_request(sender, msg)`（`atapp.h` 声明，`atapp.cpp` 定义） | 触发 `set_evt_on_forward_request` 注册的处理函数 |
| 投递 ack | `app::trigger_event_on_forward_response(sender, msg, error_code)` / `on_receive_forward_response(direct_source_id, handle, type, msg_sequence, error_code, data, metadata)` | 与回包不同通道 |
| 消息结构 | `app::message_t` / `app::message_sender_t`（`atapp.h`） | `type/message_sequence/data/metadata`；`direct_source_id/id/name/remote` |
| server frame 入站处理 | `logic_server_setup.cpp` `set_evt_on_forward_request(app_default_handle_on_receive_request)` | 回包由此进入 `ss_msg_dispatcher` |
| 事件泵 | `app::run_noblock(max_event_count = 20000)`（`atapp.cpp`） | 返回 0 无事件 / 1 有 pending / 负数错误；`tick()` 跑 `mod->tick()`+`bus_node_->proc()`+timers 但不调 `uv_run`，无 socket IO；泵内需 `atfw::util::time::time_utility::update()` + generation barrier |
| task 强制清理 | `task_manager::kill_all`、`logic_server_common_module::cleanup` | 无公开 per-task kill；hard timeout 必须 poison runtime 并 kill-all |

### 16.3 SS / Router / DNS / CS / DB / UUID / 资源 / Orbit

| 事项 | 位置 | 说明 |
| --- | --- | --- |
| SS 发送出口 | `ss_msg_dispatcher.cpp` `owner->send_message(node_id, kInServerMessage, …)` | 关联 `destination_task_id`/`source_task_id` + sequence |
| SS 生成模板 | `src/templates/rpc_call_api_for_ss.cpp.mako` | unary/stream/no-wait/broadcast/user/router |
| router 真实路径 | `router_manager_base::send_msg` → `send_msg_raw` → `ss_msg_dispatcher::send_to_proc` | 传输不绕过 dispatcher；manager 自动注册；子类须实现完整纯虚集：manager 的 `name()`/`get_base_cache()`/带 `io_task_guard&` 的 `mutable_cache`/`mutable_object`/`remove_cache`/`remove_object`，object 的 `name()`/`pull_object`/`save_object`/`save(带 io_task_guard&)`（`pull_cache`、不带 guard 的 save 非纯虚） |
| DNS 入口/seam | `src/server_frame/rpc/dns/lookup.*` → `ss_msg_dispatcher::send_dns_lookup` → `uv_getaddrinfo` | 完成走 `dns_lookup_callback` → `rpc::custom_resume`；rpc type 为 `get_dns_lookup_rpc_type()` |
| CS 入站 | `cs_msg_dispatcher::dispatch(source, msg)` | 要求 `msg.type == service_type::kAtGateway`，body 为 `atfw::gateway::server_message`；`kAddSession/kPost/kRemoveSession/kSetRouterRsp` |
| CS 下行 4 调用点 | `cs_msg_dispatcher.cpp` `send_kickoff`/`send_set_router`/`send_data`/多会话版 `broadcast_data` 各调 `owner->get_bus_node()->send_data()` | 需收敛为单一内部 gateway-send 再 hook；多会话版 `session_ids` 参数当前未生效 |
| DB 逻辑 seam | `src/server_frame/rpc/db/hash_table.*`（全部本地校验后、Redis 参数构造/发送前） | 同步 `handled + result code`，`RPC_DB_RETURN_CODE`（= `RPC_RETURN_TYPE`）包装；batch 由 get_all/partly_get 组合；v1 无 artificial latency |
| DB wire 出口盘点 | `src/` 内 `db_msg_dispatcher::send_msg` 的直接调用 | 当前只出现在 `hash_table.cpp`，没有第二条生成 DB API 出口 |
| DB 测试身份 | `db_msg_dispatcher` 构造/getter（`get_record_prefix()`/`get_db_channel_type()`） | 未 init 时 prefix 为空、channel 为 `MAX`（init 会把 prefix 置为 host-id 派生值）；hooks-on 测试 API 只设置既有字段，不启动 I/O |
| DB 内嵌 Lua | `db_msg_dispatcher.cpp` `kCompareAndSetHashTable`、`kAddListIndexHashTable` | CAS 与 KL 裁剪的 golden 语义；`CAS_FAILED→EN_DB_OLD_VERSION` 映射在 dispatcher，内存 backend 须等价复现 |
| DB init（须跳过） | `db_msg_dispatcher::init` → `cluster_init`/`raw_init`；`SCRIPT LOAD` 在连接建立回调 | 默认内存 DB 不 init 即无 Redis I/O 与 timer |
| UUID 入口 | `src/server_frame/rpc/db/uuid.*` `generate_standard_uuid`/`_binary`/`generate_short_uuid`/`generate_global_increase_id`/`generate_global_unique_id` | 前三类无 DB；`g_unique_id_pools` 静态号段（含协程排队） |
| 资源 seam | `src/server_frame/config/src/excel_config_wrapper.cpp` `excel_config_wrapper_reload_all`、`excel_config_callback_get_buffer/_version` | loader 仅首次 init 设置一次（需 `excel().enable()`）；回调内部优先 active provider；group callback 无 remove API |
| config target 方向 | `generate_config_codes.cmake`、`src/server_frame/CMakeLists.txt` | `server_frame-config` 独立成库，主 `server_frame` PUBLIC 链接它；资源 registry 必须留在 config DLL，不能反向依赖主库 |
| task/协程统一 | `rpc::async_invoke`（`rpc_async_invoke.h`）、`task_type_trait`（`dispatcher/task_type_traits.h`）、`RPC_AWAIT_*/RPC_RETURN_*`（`rpc/rpc_common_types.h`） | 两种协程模式统一 |
| 内置 SS RPC | `src/server_frame/protocol/private/protocol/pbdesc/svr.protocol.proto` | `set_server_time` 为 stream/no-wait/broadcast；`router_transfer` 是普通 unary 且无 `router_rpc: true`；router 分支需 test-only proto |
| DB smoke API | `src/server_frame/rpc/db/local_db_interface.atfw.gen.*`、`svr.local.table.proto` | `rpc::db::login_auth::replace/get_all` 操作启用 CAS 的 `PROJECT_NAMESPACE_ID::table_login_auth` |
| Orbit server RPC | `src/component/orbit/sdk/**`、`orbit_server_manager` | 生成调用和 server-to-client 封装最终进入 `ss_msg_dispatcher` |
| Orbit client 直连 | `GameSharedComponent/Orbit` 的 `OrbitClientRuntime::send_message`、`OrbitRPCDispatcher` | 唯一生产 direct-atbus 例外；pending map 在 send 前注册，response/retry/timeout 有原生完成路径 |
| telemetry 配置 | `src/server_frame/rpc/telemetry/rpc_global_service.cpp`、`svr.telemetry.config.proto`（`logic.telemetry`） | exporter 启用条件因类型而异（`ostream` 非空字串；`prometheus_push` 需 host+port+jobname；`prometheus_pull` 判 url；其余判 endpoint/file_pattern）；无 exporter 回退 Noop；trace provider 需 `has_exporters() && has_processors()`（simple/batch 均可）；file-only 纯配置可达 |
| HPA 启用条件 | `svr.hpa.config.proto`（`logic.hpa`）；`logic_server_setup.cpp` `setup_hpa_controller` | `controller.enable && metrics.enable` 默认 false → 零网络；对象/curl 句柄无条件创建但无 IO |
| HPA pull seam | `logic/hpa/pull/prometheus/logic_hpa_puller_prometheus.cpp` `do_pull()`（无参返回 bool）；`logic_hpa_policy::trigger_event_on_pull_result(logic_hpa_pull_result&)`（公开，形参是基类）；`logic_hpa_controller::create_http_request(gsl::string_view)`（唯一 HTTP 创建点，返回老名 `util::network::http_request::ptr_t`） | hook 短路 `do_pull`；注入经 `logic_hpa_pull_result_prometheus::parse(gsl::string_view)` + `trigger_event_on_pull_result`；puller 创建门槛是 `has_prometheus_http_api()` 而非 url 非空 |
| HPA etcd 写 | `logic/hpa/logic_hpa_discovery.cpp` `set_value` → `get_raw_etcd_ctx().create_request_kv_set` | 依赖真实 etcd；v1 不 mock，测试保持 `controller.configure_key` 为空 |

### 16.4 外部组件依赖离线性

| 事项 | 位置 | 说明 |
| --- | --- | --- |
| etcd init 触网 | `service_discovery_module::init`、`etcd_module::check_keepalive_actor_start_success`（阻塞 `uv_run`） | 仅 `atapp.etcd.enable`+hosts 时发生；fixture 不配；orbit agent 强依赖 etcd（v1 不支持） |
| atbus listen/proxy | `app::setup_atbus`；libatbus `channel_io_stream.cpp` | 空 listen 零 socket；主机名触发 `uv_getaddrinfo`；`bus.proxy` init 阻塞连 atproxy |
| Redis 唯一入口 | `db_msg_dispatcher::init` → `cluster_init`/`raw_init` | fixture 不 init 即无连接 |
| 日志/时间 | `atapp_log_sink_maker`（file/stdout/stderr/syslog）；`atfw::util::time::time_utility` | 无远程 sink；无 NTP |
| 组件 SDK init | dtmq/rank/distributed_transaction/orbit server SDK | init 均不触网；Orbit client SDK 的主动连接属 client 侧 |
