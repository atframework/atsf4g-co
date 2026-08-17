# 分布式事务单元测试执行计划

## 0. 实施状态记录（2026-08-17）

> 本节由实施阶段维护。状态分为：✅ 已实施并有 fresh 证据、
> ⛔ 未实施（含原因）。所有用例名保留 DT-ID，便于与本表对照。

### 0.1 结论

- 第 3 节测试基建全部落地；第 6 节 DT-001..DT-024 全部有独立用例且全绿（修复记录见 0.4）。
- 第 4/5 节矩阵已按本文要求实施；剩余缺口见 0.5。
- cpplint（仓库 CPPLINT.cfg，120 列）：本组件全部测试与改动的组件/框架文件 **0 error**。
- clang-tidy（仓库 .clang-tidy）：测试文件可修告警 **0**；仅余两类框架基线告警（见 0.6）。
- C++ 标准兼容（0.7）：本组件代码在 c++14/17 下无自身编译错误，但依赖框架（atframe_utils
  使用 `std::iter_difference_t` 等 C++20 特性）要求 **最低 C++20**（与仓库 CMAKE_CXX_STANDARD=20 一致）；
  c++20 与 c++latest（≈C++26 模式，clang-cl 19）全部编译通过，并顺手修复了
  `server_frame/rpc/telemetry/rpc_trace.h` 非限定 friend 声明的 C++20 可移植性问题。
- §7.2 Linux ASan+LSan 门禁：已通过（见 0.8）。

### 0.2 测试基建与 fresh 基线

| target | 用例数 | 标签 | 状态 |
| --- | ---: | --- | --- |
| `atf4g-co-component-distributed-transaction-api-unit-test` | 21 | fast | ✅ |
| `atf4g-co-component-distributed-transaction-client-unit-test` | 11 | fast | ✅ |
| `atf4g-co-component-distributed-transaction-participator-unit-test` | 22 | fast | ✅ |
| `atf4g-co-component-dtcoordsvr-unit-test` | 13 | fast | ✅ |
| `atf4g-co-component-dtcoordsvr-stop-unit-test` | 1 | fast | ✅ |
| `atf4g-co-component-distributed-transaction-stress-unit-test` | 1 | stress | ✅ |

fresh 命令与结果（2026-08-17，Windows Debug / Ninja）：

```
ctest --test-dir build_jobs_cmake_tools -L distributed-transaction --output-on-failure
100% tests passed out of 6（69 用例，约 7s）
ctest --test-dir build_jobs_cmake_tools -L rpc-unit-test
100% tests passed out of 17
```

环境注意：从 Git Bash 直接构建需先 source VS/SDK 的 `INCLUDE`/`LIB` 环境脚本
（`build_jobs_cmake_tools/_agent_tmp/build_env.sh`），否则链接 `Bcrypt.lib`、编译标准库头失败。

### 0.3 DT-001..DT-024 回归矩阵状态（全部 ✅）

| ID | 用例（target 前缀省略） |
| --- | --- |
| DT-001 | participator.`first_prepare_non_arena_preserves_storage_dt001` |
| DT-002 | client.`retry_exhaustion_never_fakes_commit_dt002`（RED→修复①→GREEN） |
| DT-003 | api.`replication_counts_only_valid_success_dt003` |
| DT-004 | dtcoordsvr.`manager_terminal_and_participator_acks_dt018`（remove 失败段） |
| DT-005 | participator.`terminal_action_failure_is_bounded_dt005_dt020` |
| DT-006 | client.`coordinator_terminal_is_only_decision_dt006` |
| DT-007 | dtcoordsvr.`manager_create_query_ttl_and_eviction`（memory_only 段） |
| DT-008 | participator.`appended_lock_is_dumped_and_released_dt008` |
| DT-009 | dtcoordsvr.`manager_create_query_ttl_and_eviction`（TTL 段：35/6/15/30d 钳制） |
| DT-010 | client.`set_data_and_add_participator_dt010` |
| DT-011 | participator.`allow_retry_does_not_enter_running_dt011` |
| DT-012 | api.`initialize_new_transaction_defaults_dt012` + `_custom_options_dt012` + `_replication_nodes` |
| DT-013 | api.`timestamp_exact_nanosecond_carry_dt013` |
| DT-014 | client.`terminal_delivery_result_is_separate_and_bounded_dt014` + `_dt014_symmetric`（REJECTED 对称） |
| DT-015 | participator.`resolve_budget_exhaustion_rejects_dt015` |
| DT-016 | participator.`finished_ack_failure_has_budget_dt016` |
| DT-017 | api.`chrono_duration_helper_returns_duration_dt017` |
| DT-018 | dtcoordsvr.`manager_terminal_and_participator_acks_dt018` + `participator_ack_repeated_and_after_global_terminal_dt018` |
| DT-019 | dtcoordsvr.`manager_timeout_reject_save_and_cache_invalidation_dt019` |
| DT-020 | participator.`terminal_action_failure_is_bounded_dt005_dt020`（RED→修复②→GREEN） |
| DT-021 | participator.`resolve_query_committed_dt005_dt021` |
| DT-022 | participator.`lock_wound_and_preemption_dt022` |
| DT-023 | client.`force_commit_does_not_require_commit_callback_dt023` |
| DT-024 | client.`legacy_constructor_and_layout_compatibility_dt024` |

### 0.4 实施中确认并修复的缺陷（RED→GREEN）

1. `transaction_client_handle::submit_transaction`：入口未检查过期时间，已过期事务仍会 create
   协调者记录（协调者会把过期记录按默认超时“复活”）。现入口直接返回 `EN_SYS_TIMEOUT`（第 5.2.4 节）。
2. `transaction_participator_handle::resolve_transcation`：query 阶段合入 COMMITED/REJECTED 时
   无条件清零 `resolve_times`，timer 驱动的每次重入都拿到全新重试次数，`do_event` 失败重试不收敛
   （DT-020）。现仅在首次进入本地动作阶段（`local_action_stage_entered`）时重置一次。
3. `transaction_manager::mutable_transaction`：读取失败（DB 错误 / 记录 Any 类型不符）时
   `out` 残留指向未填充缓存对象，超时判定把空数据伪造成 REJECTED 并写回 DB，吞掉原始错误且返回成功
   （新增 dtcoordsvr.`mutable_transaction_bad_any_unpack` 先 RED）。现读取失败时跳过超时判定并清空输出句柄。
4. `transaction_manager::save`：空句柄传入会在 `await_save` 内解引用崩溃，现入口返回 `EN_SYS_PARAM`
   （dtcoordsvr.`lru_unit_test_seam` 覆盖）。
5. `server_frame/rpc/telemetry/rpc_trace.h`：非限定 `friend class context;` 按 C++20 缺陷修复规则
   指向当前命名空间内的新类型而非 `rpc::context`，clang C++20 模式报错（MSVC 宽容掩盖）。
   已改为限定名 `friend class rpc::context;`。

### 0.5 未实施项（⛔）

- §3.1 的 task create/start 失败注入 seam：需要在 task_manager 中增加
  `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` 门控的注入点，涉及面广；对应错误分支均为简单返回路径。
  未注入前相关用例按 §3.1 标注 blocked。

### 0.6 lint 基线说明

- cpplint：0 error（含全部 8 个测试文件、测试公共头、组件改动文件、`rpc_lru_cache_map.h`）。
- clang-tidy（仓库 .clang-tidy，clang-tidy 19，`--header-filter` 限定本组件路径）：
  测试文件可修告警 0。以下两类为框架基线告警，非测试代码可修：
  1. `CASE_TEST` 宏展开生成的静态注册对象（non-const global / anonymous namespace / throwing static
     initialization）——来自 atframe_utils 私有测试框架，仓库全部存量测试同样携带；
  2. `cppcoreguidelines-init-variables` 对 `int32_t res = RPC_AWAIT_CODE_RESULT(...)` 的误报
     （co_await 初始化器未被识别）——rpc-unit-test selftest 同样携带（rpc_unit_test_db.cpp 10 处）。
- 组件/框架文件经 tidy 检查仅余 1 条位于 **生成** 的 `distributed_transaction.pb.h` 内的
  analyzer 告警（EnumCastOutOfRange，codegen 产物，按仓库规则不手改生成文件）。

### 0.7 C++ 标准兼容矩阵（clang-cl 19 `-fsyntax-only`）

| 标准 | 本组件自身代码 | 整体编译 | 说明 |
| --- | --- | --- | --- |
| C++14 | 0 error | ✗（20 处均在 vendored atframe_utils/libatapp 头，如 `std::iter_difference_t`） | 依赖要求 C++20，14/15/17/18/19 不可用 |
| C++17 | 0 error | ✗（同上） | 同上 |
| C++20 | 0 error | ✅ 全部 TU | 项目当前标准（CMAKE_CXX_STANDARD=20） |
| C++latest（≈26 模式） | 0 error | ✅ 全部 TU | 含修复后的 rpc_trace.h |

结论：组件与测试代码自身对 C++14→26 语法兼容；实际可用下限由依赖框架决定为 **C++20**，
与仓库构建配置一致。round-2 修复：`rpc_trace.h` 非限定 friend（0.4 第 5 条）；
测试 TU 显式补齐原先依赖 PCH 传递的头（`utility/protobuf_mini_dumper.h`、`memory/object_allocator.h`），
脱离 PCH 也可独立编译。

### 0.8 Linux ASan+LSan 门禁（WSL+Debian 执行记录，2026-08-17 ✅）

环境：WSL Debian，`/home/owent/workspace/github/atframework/atsf4g-co`（fast-forward 到与 Windows
同源提交后以补丁同步未提交改动）。构建树 `build_jobs_cmake_tools/_agent_tmp/distributed-transaction-asan`：
GCC 14 + Ninja + `BUILD_SHARED_LIBS=ON` + `PROJECT_ENABLE_UNITTEST=ON` +
`PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS=ON` + `PROJECT_SANTIZER_USE_ADDRESS=ON`（Debug）。

```
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_odr_violation=0 ctest -L distributed-transaction --output-on-failure
100% tests passed, 0 tests failed out of 6（69 用例，含 1000 轮 stress）
泄漏/use-after-free/double-free/越界报告数：0
```

门禁实测发现并修复的 3 个问题（均为 RED→GREEN）：

1. 测试代码：`participator_event_recorder{}.make_vtable()` 的 vtable lambda 按指针捕获临时 recorder，
   临时对象当场析构后 prepare 回调写穿悬空指针（GCC+ASan 报 stack-use-after-scope；MSVC 未插桩静默通过）。
   已改为与 handle 同生命周期的具名 recorder（DT-022 reload 与 load_dump_round_trip 两处）。
2. 组件代码：`transaction_manager.cpp` save lambda 内 `ret` 遮蔽外层同名变量（GCC `-Werror=shadow`），
   已更名 `sub_ret`。
3. 测试公共头：`dt_test_common.h` 的 `set_nanos` 纳秒取模结果未显式收窄（GCC `-Werror=conversion`），
   已加 `static_cast<int32_t>`。

第三方抑制（单项记录，非全局关闭泄漏检测）：grpc v1.82.1 共享库构建中 `libgrpc.so` 与
`libupb_descriptor_lib.so` 重复注册同一 upb 全局（`google_protobuf_descriptor_proto_upb_file_layout`），
ASan 报 odr-violation；两者均为第三方库内部符号、与本组件无关，故按 7.2 节规则使用
`detect_odr_violation=0` 豁免该类别，泄漏检测保持全开。

## 1. 文档状态与证据边界

- 编制日期：2026-08-14。
- 适用目录：`src/component/distributed_transaction`。
- 本计划基于当前源码、当前生成的 RPC/DB 接口和当前 `rpc-unit-test` 测试夹具，不把未实现能力写成已存在能力。
- 本计划采用维护者确认的终态契约：协调者持久化的 `COMMITED`/`REJECTED` 是全局结果的唯一真相；
  Client 和参与者后续错误不能改变该结果，只能进入有界重试和最终消费流程。单次任务、单个事务都必须有明确的
  尝试次数、退避间隔和截止条件，不能在协程内死循环，也不能永久保留 running/finished/DB 状态。
- 当前基线命令与 fresh 结果见第 0.2 节（6 个 target、69 用例，Windows Debug 与 Linux ASan 双侧全绿）。
- `transaction_client_handle` 和 `transaction_participator_handle` 在当前仓库内没有组件外调用方；测试契约只能以
  本目录 public API、protobuf 注释和实现行为为准，不能从尚不存在的业务接入代码推导额外语义。
- 本文第 6 节按 DT-001..DT-024 一对一列出 target、case、故障注入和核心断言；实施状态见第 0 节。

## 2. 已验证的设计与代码流程

### 2.1 角色与持久化边界

| 角色 | 当前职责 | 主要证据 |
| --- | --- | --- |
| 发起者 client | 初始化事务、顺序 prepare 参与者、提交协调者、通知参与者最终结果；锁冲突时随机退避 | `sdk/transaction_client_handle.cpp:59-273` |
| 协调者 dtcoordsvr | 保存完整 `transaction_blob_storage`，通过 CAS 更新全局状态和参与者状态，最后删除 DB 记录；LRU 只做弱缓存 | `dtcoordsvr/logic/transaction_manager.cpp:83-484` |
| 参与者 | 保存本地运行/完成快照、管理资源锁、执行 Do/Undo、查询协调者恢复状态、重发最终确认 | `sdk/transaction_participator_handle.cpp:46-935` |
| 持久化表 | `(zone_id, transaction_uuid)` KV，启用 CAS，数据主体放在 `Any blob_data` | `src/server_frame/protocol/private/protocol/pbdesc/svr.local.table.proto:135-150` |

协调者不是事务执行者。正常路径是：

1. client 创建本地事务对象，状态为 `CREATED`，写入 UUID、超时和重试配置。
2. client 将状态改为 `PREPARED`，非 `force_commit` 模式先调用协调者 `create`，协调者写 DB 并放入 LRU。
3. client 逐个调用参与者 prepare；参与者通过回调做预检查/锁定，并把事务加入本地 running 快照。
4. 全部 prepare 成功后，client 调用协调者 `commit`；只有协调者 CAS 持久化并返回全局 `COMMITED`，事务才被视为
   成功。此后 Client/参与者的任何错误都是“成功结果的投递与消费错误”，不能把事务改成失败。
5. client 按同一终态有界重试通知每个参与者。参与者执行 `do_event`；失败时保留可恢复状态并按配置重试，达到预算后
   记录失败并按协调者的成功终态完成确认和本地消费，不能无限保留事务。
6. 参与者 `tick()` 是 Client 投递失败/进程退出后的恢复路径：查询协调者终态，执行同方向本地动作，再有界重试
   `commit_participator`/`reject_participator`。成功、幂等 notfound 或重试预算耗尽后都必须结束本地生命周期；协调者在
   全部确认后删除 DB，兜底 TTL/扫描负责永久失联场景。

prepare 或协调者决策失败同理：Client 必须先让协调者持久化 `REJECTED`，再按失败终态有界通知参与者；不能用一次
本地 RPC 错误直接推导全局失败，也不能在未确认终态时直接删除协调者记录。协调者请求“服务端已落库但响应丢失”时，
Client 只能把结果视为 unknown 并进行有界 query；若本次调用仍无法确定，应返回 pending/unknown 类错误并退出，把恢复交给
持久化状态和参与者定时任务，不能发送相反方向通知。

### 2.2 状态机断言

协议数值是测试必须固定的兼容契约（`protocol/protocol/pbdesc/distributed_transaction.proto:21-31`）：

| 数值 | 名称 | 测试解释 |
| ---: | --- | --- |
| 0 | `CREATED` | client 尚可写 transaction data/participants |
| 1 | `PREPARED` | 协调者已创建、参与者 prepare 阶段 |
| 2 | `FINISHED` / `REJECTING` | `FINISHED` 是不可回退边界；参与者拒绝确认尚未完成 |
| 3 | `COMMITING` | 参与者提交确认尚未完成 |
| 4 | `REJECTED` | 全局或参与者拒绝终态 |
| 5 | `COMMITED` | 全局或参与者提交终态 |

所有状态测试都必须同时断言：返回码、全局 metadata 状态、每个 participator 状态、DB 版本/记录是否存在、参与者
running/finished/lock 三组容器。只断言返回码不算覆盖。

### 2.3 终态权威与有界消费不变量

每个正常、错误和容灾用例都必须同时验证以下不变量：

1. **全局终态唯一**：协调者一旦是 `COMMITED`/`REJECTED`，Client 不得反转全局结果；participant ack 不改变全局
   metadata。按 D2，参与者可独立更新自己的状态，但自身终态不可反向回退，且只有全部参与者达到同一方向才删除记录。
2. **结果与投递分离**：`submit_transaction()` 的事务结果取自协调者终态，不取自最后一次参与者通知返回码。投递失败
   通过单独的输出、日志/指标和恢复状态表达。
3. **每次执行有界**：Client 终态通知、参与者查询、Do/失败清理和 participant ack 都使用配置化尝试上限与
   `resolve_retry_interval`；每次 wait 和 RPC 还有 task deadline。任何一条路径都不得出现无界循环。
4. **最终消费**：达到重试上限仍失败时，记录诊断并清理本地 running/finished/lock/timer。协调者记录最终由全部同向
   ack 或 TTL 清理，不能永久增长。
5. **unknown 有界处理**：协调者 RPC 超时/断连时 Client 不得猜测终态；参与者按 D6 在
   `resolve_times > resolve_max_times` 后进入本地 reject 消费。两者都必须有界退出，不能在同一协程或后续 tick 中无限循环。

跨任务的最终上界由 `expire_timepoint + coordinator reject grace + participant consume budget + DB cleanup TTL` 共同限定；
测试必须推进到这个绝对截止点并证明本地与 DB 都已消费。只证明“单次 task 退出”不等于满足不能无限重试。

测试中 `resolve_max_times=N` 固化为每个恢复阶段最多 N 次实际尝试（`N=0` 先归一化为默认值）；阶段从“查询终态”切换到
“执行本地终态动作”或“发送 participant ack”时计数重置。Client 终态通知也使用同一实际尝试上限。这样可以覆盖每个
阶段又保证总量有限；如果实现决定采用“首次尝试 + N 次重试”的定义，必须同步修改协议注释和全部边界断言，不能混用。

### 2.4 一致性和路由模式

- `R=0,N=0`：依赖 DB CAS；按 metadata 中仍存活的 replica id 或 UUID consistent-hash 选一个协调者
  （`sdk/rpc/transaction/transaction_api.cpp:51-83`）。
- `R>0,N>=R`：初始化时选择最多 N 个协调者，RPC fan-out，等待 R 个有效响应并合并 metadata/storage
  （`transaction_api.cpp:86-136,278-351`）。
- discovery 测试节点必须含 `hpa_scaling_ready=1`，注入后调用
  `logic_server_last_common_module()->reload()`；这是当前测试夹具和现有用例已经验证的必要步骤。
- replication 用例必须至少注入 3 个不同 node id；只放 1 个节点无法证明 quorum、部分失败或 failover。

### 2.5 时间类型边界

当前协议源已满足 well-known type 要求，无需再改字段类型或编号：

- `transaction_metadata.prepare_timepoint/finish_timepoint/expire_timepoint` 和
  `transaction_participator_storage.resolve_timepoint` 已是 `google.protobuf.Timestamp`；
- `resolve_retry_interval/lock_wait_interval_min/lock_wait_interval_max`、dtcoordsvr 的 LRU/默认 timeout 已是
  `google.protobuf.Duration`。

需要迁移的是 C++ 边界和实现：

- time point 统一使用 `std::chrono::system_clock::time_point`，duration 统一使用
  `std::chrono::system_clock::duration`（或具体的 `std::chrono::seconds/milliseconds` 输入）；
- `transaction_options` 已使用 chrono；`time_utility::raw_time_t/raw_duration_t` 也已核实是上述两种类型的 alias；
- `transaction_api::initialize_new_transaction()` 的 public `timeout` 已改为
  `std::chrono::system_clock::duration`；测试需覆盖该导出 API/ABI 变更及全部调用方；
- 所有协议边界转换已经统一使用 `protobuf_to_system_clock()`、`protobuf_from_system_clock()`、
  `protobuf_to_chrono_duration()` 和返回 `google::protobuf::Duration` 的 `protobuf_from_chrono_duration()`；测试需补共享 helper
  的正负值、亚秒值和整秒边界回归；
- 本组件手写 `seconds()/nanos()` 时间运算和本地 `std_duration_to_protobuf_duration()` 已删除；测试需验证 1e9 纳秒边界、
  亚秒配置和负 duration 不发生精度或规范化回归。

## 3. 测试分层与目标拆分

在 `test/CMakeLists.txt` 中把测试拆成四个 executable，避免 process-lifetime singleton 和故障注入相互污染：

| 建议 target | Category / Features | 源码范围 | 原因 |
| --- | --- | --- | --- |
| `${PROJECT_NAME}-component-distributed-transaction-api-unit-test` | `sdk` / `SS` | `transaction_api`、生成 RPC mock、merge/pack | discovery 和 replication 规则集中，独立控制 SS 响应序列 |
| `${PROJECT_NAME}-component-distributed-transaction-client-unit-test` | `sdk` / `SS` | `transaction_client_handle` | vtable 回调脚本、随机退避和输出集合不污染参与者状态 |
| `${PROJECT_NAME}-component-distributed-transaction-participator-unit-test` | `sdk` / `SS` | `transaction_participator_handle`、resolve action | running/finished/lock/timer/auto-task 生命周期集中验证 |
| `${PROJECT_NAME}-component-dtcoordsvr-unit-test` | `service` / `SS DB` | manager、7 个 action 源文件、service test | 协调者实现只编进 service executable，需像 `component/dtmq/test` 一样直接编入测试 target |

以上 target 和单独的 stop target 均通过 `project_add_rpc_unit_test()` 创建，并设置
`LABELS fast distributed-transaction`；1,000 轮压力用例另设 target，使用
`LABELS stress distributed-transaction`，避免默认 fast 回归被长用例拖慢。

另外把 `transaction_manager::stop()` 放入单独的
`${PROJECT_NAME}-component-dtcoordsvr-stop-unit-test`。`transaction_manager` 是进程生命周期 singleton，调用
`stop()` 后 `mutable_transaction()` 永久返回 `EN_SYS_SERVER_SHUTDOWN`；不能让该用例影响同进程其他 case。

协调者测试 target 还需：

- 链接 `components::distributed-transaction-sdk` 和 `components::dtcoordsvr-config`；
- `target_include_directories(... PRIVATE "../dtcoordsvr")`，保持与 service target 的相对 include 行为一致；
- 在 `runtime_options.setup_callback` 中注册与 `dtcoordsvr_main.cpp:79-84` 相同的 server-instance config loader；
- runtime 启动后调用
  `test.db().register_message_type<PROJECT_NAMESPACE_ID::table_distribute_transaction>()`；
- public manager 行为直接调用 manager；7 个已知 action 用
  `atframework::testing::invoke_ss_action<TAction>` 驱动真实序列化、task 和 action；只有 RPC 注册、非法 envelope、
  response envelope 捕获才使用 raw transport/dispatcher。

### 3.1 必要的最小测试 seam

以下失败无法用现有 public fixture 稳定制造，不能用“耗尽内存/碰运气停任务”替代：

- `task_manager::create_task` / `start_task` 指定失败。

若该路径必须纳入自动化，增加由 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` 门控的 test-only 一次性失败注入
hook。生产构建必须完全裁剪，且 hook 自身先由 rpc-unit-test selftest 覆盖。未增加 seam 前，该用例必须在结果中
标成 blocked，不能记为通过。

已提供的 seam：`transaction_manager::get_lru_size_for_unit_test()` / `clear_lru_for_unit_test()`
（同样由 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` 门控，生产构建完全裁剪），用于在 case 间读取/清理
进程级单例的 LRU 状态（dtcoordsvr.`lru_unit_test_seam`）。

## 4. 公共接口覆盖矩阵

### 4.1 `rpc::transaction_api`

| Public API | 必测用例与断言 |
| --- | --- |
| `initialize_new_transaction` | C++ 参数为 chrono duration；UUID 非空且唯一；0 timeout 回退 10s；正/负/亚秒/跨秒 duration；CREATED；memory/force 标志；R/N 为 0、合法、N<R、节点数<N；所有 Timestamp/Duration 合法归一化 |
| `query_transaction` | 单节点成功/notfound/无 discovery/timeout/transport error；replica R-of-N 成功、部分 error、空 body、malformed body、响应乱序；多副本字段 merge；相反终态冲突只打印错误日志并确定性收敛到较后终态（COMMITED 优先），不返回错误 |
| `create_transaction` | 非 PREPARED、resolve_max=0、无节点；普通成功；replica quorum；单次调用不重试（`EN_DB_OLD_VERSION`/`EN_DB_KEY_EXISTS` 直接返回）；请求对象在 heap/Arena/跨 Arena 时调用后保持不变 |
| `commit_transaction` | 成功并更新 inout metadata；单次调用：`EN_DB_OLD_VERSION` 与普通 error 都直接返回不重试；目标 failover；已是 COMMITED 幂等；返回 REJECTED 时保留协调者真相且调用方不得通知 commit |
| `reject_transaction` | 与 commit 对称；已 REJECTED 幂等；返回 COMMITED 时保留协调者真相且调用方不得通知 reject |
| `remove_transaction_no_wait` | 空 UUID；单节点 one-way 发送；replica 至少 R 次发送；即时 send error 的成功阈值 |
| `remove_transaction` | notfound 幂等成功；单次调用：`EN_DB_OLD_VERSION` 直接返回不重试；DB/transport error；replica quorum |
| `commit_participator` | key/metadata 打包；成功 merge；notfound；单次调用：`EN_DB_OLD_VERSION` 直接返回不重试；replica quorum |
| `reject_participator` | 与 commit_participator 对称 |
| 两个 `merge_storage` | 空输出、部分输出、状态单调合并、participant union、Any 首个有效值、metadata/configure 不丢字段、缺失 key 不修改输出；全局/参与者相反终态冲突打日志后仍合并为较后终态 |
| 三个 `pack_participator_request` | prepare 完整复制 metadata/config/data；commit 只带 UUID；普通 reject 只带 UUID；force reject 带完整 Undo 数据 |

生成的 7 组 `packer::{pack,unpack,get_full_name}`、node/id 两种 unicast overload 和 7 个 typed `mock` 注册接口也都是
public surface。每组至少做一次 pack/unpack round-trip、正确 full-name、空 handler 返回空 rule、rule RAII 失效、node 与 id
overload 等价；不重复测试生成器已经覆盖的模板细节。

### 4.2 `transaction_client_handle`

| Public API / callback | 必测用例与断言 |
| --- | --- |
| ctor/dtor、private data、destroy callback | 保持上游直接构造接口和对象布局；null/non-null vtable；栈对象与 strong_rc_ptr 两种持有方式；set/get 指针；析构回调恰好一次；回调内不发生二次析构 |
| `create_transaction` | 默认 options 与所有自定义 options 精确映射；输出旧指针被替换且可释放 |
| `set_transaction_data` | null input；CREATED 成功 PackFrom；PREPARED 及终态拒绝 |
| `add_participator` | null、空 key、首次添加、重复 key 更新、PREPARED 后拒绝；map key 与内部 participator_key 一致 |
| `submit_transaction` | 0/1/N 参与者；force_commit；create 失败回滚；协调者 create/commit/reject 对 `EN_DB_OLD_VERSION`（create 含 `EN_DB_KEY_EXISTS`）有界重试（上限 5 次）后成功/耗尽失败；任一参与者 prepare 失败时先持久化协调者 REJECTED（不要求覆盖每个失败位置，部分失败即可）；协调者 COMMITED 后无论通知如何都返回事务成功；REJECTED 后返回事务失败；commit/reject 回包丢失时有界 query，不发送相反方向；终态通知按参与者顺序执行且各自有界重试；两个输出集合每次先清空；`output_failed_participators` 仅含 prepare 失败参与者，终态通知投递失败不进入该集合（由参与者 resolve 流程保证最终一致） |
| `prepare_participator` | success；普通错误；`PREEMPTED+allow_retry`；`success+allow_retry`；退避 min=max/min<max；到期前和已到期；最大重试边界 |
| `commit_participator` / `reject_participator` | 非 force 通知方向取协调者终态且只 reject 已 prepare 参与者；每个参与者最多 `resolve_max_times` 次实际尝试并按 interval 退避；第 1/N 次成功、始终失败、超时/取消；投递失败不能覆盖全局结果；force_commit 由 Client 本地按 `prepare_complete` 决策，已 prepare 参与者有界 undo，投递不确定的 failed participant 只补发一次 undo，明确失败和其他未 prepare 参与者不通知 |

client 用例必须把 vtable 调用记录为有序事件列表，例如
`create -> prepare(A) -> prepare(B) -> coord_commit -> commit(A) -> commit(B)`；只计次数会漏掉顺序错误。

### 4.3 `transaction_participator_handle`

| Public API / callback | 必测用例与断言 |
| --- | --- |
| ctor/dtor、private data、key | key 保真；destroy callback 一次；无活动 task 时 strong_rc 能归零 |
| `load` / `dump` | 空、running、finished、锁、timer 的 round-trip；重复 UUID；冲突资源；load 覆盖旧状态而不是追加；dump 后 Any/配置完整；running REJECTING 恢复为 `wounded=true`，重启后仍禁止 commit 并转入本地 reject |
| `check_writable` | 无 callback 默认 true；callback true/false/error；resolve task 在 false 时不消费任何队列 |
| `prepare` | 空 UUID；check_prepare success/error/retry reason；普通模式首次/重复 prepare；首次 prepare 分别使用普通非 Arena 请求和 Arena 请求，断言 storage 不丢失；force_commit 回调顺序；每个回调失败传播规则 |
| `commit` / `reject` | UUID 不存在幂等；正常 running 转 finished；重复/并发通知；参与者自身终态不可反向回退；Do/失败清理及 lifecycle callback 第 1/N 次成功和永久失败；最多 N 次后消费，不能永久保留；在途终态方向标记存于 running 条目（`inflight_terminal_direction`）且由 RAII 在所有返回路径复位，同方向重入幂等成功、反方向返回 `EN_TRANSACTION_FINISHED`；本地动作预算 `resolve_times` 只在首次进入动作阶段时重置（`local_action_stage_entered`），阶段内 RPC 重发/timer 重复进入不重置、跨驱动源累计 |
| `check_lock` | 空 UUID、终态、无锁、同事务重入；older/younger/timestamp 相同 UUID tie-break；多资源、重复资源；preemption 输出清空且准确 |
| `lock` | null/终态；首次单/多资源；已有锁后追加资源；抢占旧 holder 时其 running 条目被置 `wounded`、持久状态转为 REJECTING 并重新武装 resolve timer；被抢占事务后续 commit 返回 `EN_TRANSACTION_RESOURCE_PREEMPTED`，resolve 优先本地 reject，条目销毁（remove running）后标记随之消失；dump/load 后该方向不丢失；重复锁不产生重复 snapshot 项 |
| 4 个 `unlock` | ptr/UUID × 单资源/全部；holder 不匹配；不存在；调用后 storage.lock_resource 和 lock map 同时清空 |
| `get_locker` | unlocked/running/finished/null holder 边界，不允许悬挂 strong reference |
| `get_running_transactions` / `get_finished_transactions` | prepare、commit/reject、最终 ack 前后的集合及对象内容；running 集合值类型为 `running_transaction_entry`，断言 `entry.storage` 及 `wounded`/`inflight_terminal_direction`/`local_action_stage_entered` 标记随条目创建默认复位、随条目销毁清理 |
| `tick` | 无工作、due/not-due/equal timepoint；显式 `system_clock::time_point`；已有 auto task；task create/start 失败 seam（`trigger_due` 已在回调前移除 timer，拉起失败后断言所有已收集条目经 `schedule_resolve_retry` 按 `resolve_retry_interval` 退避重新武装、不消耗 `resolve_times` 预算、下一次 tick 可再次收集且不形成 tight loop）；统一 resolve timer 队列（query/acknowledge 单队列按 action 分发，同 UUID 至多一条、新 timer 替换旧 timer、到期条目回调前自动移除）行为不变；running query、本地终态动作、finished ack 三阶段独立有界计数/退避/耗尽消费；resolve task 内 participant ack 对 `COMMITING/COMMITED` 保持 commit 方向、对 `REJECTING/REJECTED` 保持 reject 方向，单次调用不重试，失败由 acknowledge timer 到期重新拉起整个 task；任务完成后可再次 tick且无 tight loop |
| 所有 vtable 回调 | `do_event`、`undo_event`、`check_prepare`、`check_writable`、5 个 lifecycle callback、resolve-finished 的顺序、次数、失败传播 |

### 4.4 `transaction_manager` 与 7 个协调者 RPC

| Public API / RPC | 必测用例与断言 |
| --- | --- |
| `create_transaction` / `create` | UUID 校验；DB replace（expected_version=0 无条件写）成功/错误；同 UUID 重试重放（数据相同）幂等覆盖成功；memory_only；过期时间 fallback；DB version 前进；LRU 可读 |
| `mutable_transaction` / `query` | LRU hit 不读 DB；miss 读 DB/Any unpack；notfound 映射；错误 Any；memory_only cache；超时+grace（`transaction_expire_grace_duration`，默认 5s）自动 REJECTED 后执行一次 CAS save；任意 save 错误返回错误并淘汰 cache，后续 query 从 DB 重新加载；shutdown |
| `save` | null、memory_only、replace 成功、`OLD_VERSION`、`NOT_FOUND`、普通 DB 错误；版本写回；每次 save 恰好一次 replace；任意非零结果都淘汰 cache；调用层不重复 remove_cache，notfound 映射后与普通错误共用一次日志；TTL 刷新失败不覆盖成功 CAS 返回值 |
| `try_commit` / `commit` | PREPARED -> COMMITED + finish time；重复 commit；先 reject 后 commit；CAS 冲突；response metadata |
| `try_reject` / `reject` | 与 commit 对称 |
| participant 两组 overload / RPC | key 不存在；首个/中间/最后参与者；重复 ack；全局 PREPARED/COMMITED/REJECTED 下分别发送两种 ack；全局状态始终不变，参与者可自主进入请求方向但自身终态不可反向回退；只有全部参与者严格同向完成时 delete；delete 前用 `await_io_task()` 排空在途 IO 且不产生额外 replace；remove notfound 幂等；remove error 不得丢 cache |
| `try_remove` / `remove` | 空 UUID；memory_only；DB 成功/notfound/OLD_VERSION/普通错误；remove 前 `await_io_task()` 排空在途 IO 且不产生额外 replace，排空失败仍继续删除；stream/no-wait action 禁用回包；错误时记录必须仍可读取 |
| `tick` | 空 LRU；null entry seam；duration 到期；max count；未到期不淘汰；有在途 IO 的缓存照常淘汰且置 `removed`（IO 结束后不写回/不重新入缓存）；memory_only 允许容量淘汰并打印警告日志；淘汰后从 DB reload；返回准确淘汰数 |
| `stop` | 单独 executable：stop 前可读、stop 后 fetch 返回 shutdown、重复 stop 幂等 |

每个 action 至少两类测试：

1. `invoke_ss_action<TAction>` 的有效请求，断言真实 action 副作用和最终 task result；
2. raw dispatcher 的空 UUID、未知 RPC、错误 type URL/body、stream remove，用 outbound SSMsg 解包断言 response code/body。

## 5. 故障恢复与容灾场景

### 5.1 协调者/DB

1. **创建后进程丢缓存**：create 写 DB，强制清 LRU/tick 淘汰，再 query/commit，必须从 DB 恢复同一 blob 和 CAS version。
2. **保存时 CAS 冲突/记录已删**：replace 返回 `EN_DB_OLD_VERSION` 或 `EN_DB_RECORD_NOT_FOUND`，
   断言只调用一次、立即淘汰旧 cache；SDK 后续重选节点/重试时不能把旧 cache 当成成功结果。
3. **DB 短暂错误**：save 注入普通 DB/网络错误，断言只有一次 replace、返回同一错误且 cache 淘汰；
   后续请求必须重新读 DB，不得在 `save()` 内连续 replace。get/remove 分别注入一次错误后恢复，按各自
   public 契约验证返回码和下一次调用可恢复。
4. **删除失败**：remove_all 返回普通错误或 OLD_VERSION，DB 记录和可恢复状态必须保留。
5. **协调者扩缩容**：metadata 首选 replica 下线，第二 replica 在线；普通模式选择存活节点，replication 模式满足 R 后成功。
6. **部分副本失败**：N=3，分别测试 3/3、2/3、1/3 成功；R=1/2/3；success/error/timeout/malformed/empty response
   混合且乱序。成功条件必须是“有效成功响应数 >= R”，不是“收到响应数 >= R”。
7. **全局终态竞态**：commit 与 reject 先后到达两种排列；第二个调用返回的 metadata 是既有终态，Client 继续该方向。
   相反 participant ack 不改变全局状态；参与者可更新自己的非终态状态，但自身终态不可反向回退，混合方向不得删除记录。
8. **决策成功但回包丢失**：commit/reject action 已持久化，Client 收到 transport timeout；随后 query 查回终态并只发送
   同方向通知。query 在本次预算内仍失败时返回 unknown/pending，不能自行选择相反方向。
9. **超时失败持久化**：query 触发 PREPARED→REJECTED，replace 成功后清 LRU/模拟重启，DB reload 仍是
   REJECTED；replace 返回任意错误时必须返回错误并淘汰该 cache，不能返回只存在于某个 cache 的失败状态。
10. **无参与者事务**：Client submit 和协调者 create 均返回 `EN_SYS_PARAM`，不得创建 DB/LRU 记录。

### 5.2 client

1. prepare 第 k 个参与者失败，进程仍存活：先把协调者持久化为 REJECTED，再有界 reject 已 prepare 集合；最终清理通过
   participant ack/TTL 完成，不能在终态未确认前 no-wait remove。
2. 锁冲突在第 1..max 次解除：重试后成功；始终冲突：失败且不发送 commit。
3. `allow_retry=true` 且 callback 返回 0：耗尽重试也不得当作成功。
4. 调用 submit 时已经过期：不得 create/commit/通知 commit。
5. 协调者 COMMITED 已落库，但一个或多个 participant commit 通知 send 失败：Client 最多尝试 N 次，submit 仍返回事务
   成功；参与者 timer 恢复补齐同方向动作/ack。REJECTED 路径完全对称。
6. client 在 create 后、部分 prepare 后、协调者 commit 后分别“消失”：用保留下来的协调者 DB 和参与者 snapshot 启动恢复用例。

### 5.3 参与者

1. prepare 后未收到终态，timer 到期 query 得 COMMITED/REJECTED，分别执行 Do/Reject 并重发 participant ack。
2. query 返回 PREPARED：预算内只重排 timer，不执行 Do/失败清理；当下一次进入时满足
   `resolve_times > resolve_max_times`，按 D6 直接走 `reject_transcation` 并有界消费，且不再发起 query。
3. query notfound：释放 running 与所有锁；不残留 finished/lock/timer。
4. query、本地终态动作和 ack 暂时失败：各阶段在第 1/N 次恢复成功；本地动作/ack 永久失败时恰好尝试 N 次并产生
   耗尽诊断；query 永久失败则按 D6 在预算耗尽后的下一次进入本地 reject 消费。
5. `check_writable=false`：任何 finished/pending 都不消费；恢复 true 后继续。
6. 进程重启：prepare 后 dump，销毁 handle，new handle load，再 commit/reject；回调次数和最终快照与未重启路径一致。
7. Do/失败清理在执行前失败、执行后返回错误、lifecycle callback 失败：重试方向不变；成功前不 ack，达到 N 次预算后
   记录失败、按协调者终态 ack 并消费；不得无限留在 running/finished。
8. 同一 commit/reject 重复或并发到达：成功的业务动作至多一次；失败尝试总数有上限；ack 可幂等重发但总尝试有界。

真实 Redis、etcd、atbus、跨进程网络分区和进程级 kill 不属于离线单元测试，也不在本计划范围内。不得用单元测试结果宣称这些真实依赖已验证。

## 6. 回归追踪矩阵

下表是回归问题的完整自动化覆盖索引，实施状态见 0.3（全部 ✅）。

| Issue | Target / 建议 case | 故障注入与核心断言 |
| --- | --- | --- |
| DT-001 | participator / `first_prepare_non_arena_preserves_storage` | 普通非 Arena 请求包含 UUID/Any/locks；prepare 后 output、running、dump 完整；另以 Arena 请求做跨 Arena 回归 |
| DT-002 | client / `retry_exhaustion_never_fakes_commit` | 已过期和 `success+allow_retry` 永久返回两条路径；协调者/participant commit 均为 0 次；已 create 时协调者持久化 REJECTED，并按失败方向有界消费 |
| DT-003 | api/client / `replication_counts_only_valid_success` | N=3/R=2，空 protobuf 成功响应、业务错误、malformed、timeout 乱序组合；只有有效成功数达到 R 才返回成功；少于 R 个响应合入的终态 metadata 不能让 Client 跳过 quorum query |
| DT-004 | dtcoordsvr / `remove_propagates_db_failure` | remove_all 返回普通错误/OLD_VERSION；RPC 传播错误，LRU/DB 仍可恢复；成功/notfound 才清 cache |
| DT-005 | participator / `terminal_action_failure_is_bounded_and_consumed` | 协调者分别固定 COMMITED/REJECTED；`do_event` 第 1/N 次成功及永久失败；方向不变，恰好 N 次（动作阶段内经 RPC 重发/timer 重复进入不重置计数，预算跨驱动源累计）；最终 ack/本地清理且有耗尽诊断；lifecycle 回调按 remove running→add finished→callback 顺序且失败只记录；force_commit 单独验证 Client 本地结果和 undo 范围 |
| DT-006 | client / `coordinator_terminal_is_only_decision` | commit/reject 已落库但响应丢失、相反终态回包、prepare 失败；有界 query 后只通知持久化方向，失败路径先持久化 REJECTED，不直接 remove |
| DT-007 | dtcoordsvr / `memory_only_cache_is_mutable` | memory-only create 后 query/commit/reject/ack 命中 LRU 且 0 次 DB；淘汰后的 notfound 行为单独断言 |
| DT-008 | participator / `appended_lock_is_dumped_and_released` | 同一事务依次 lock A/B，dump 含两项，unlock/终态后两个 locker 均为空且 strong pointer 回基线 |
| DT-009 | dtcoordsvr / `terminal_record_has_bounded_lifetime` | 空参与者创建被拒绝；非空事务在参与者永久失联或 Client 消失时，DB TTL 使用绝对截止时间 `expire_timepoint + transaction_expire_grace_duration`（默认 5s），晚到保存不重置 grace，且不超过 `transaction_max_ttl`（默认 30 天、硬上限 3 年），不叠加恢复预算、不允许永不过期；正亚秒 TTL 向上取整，推进后 DB/LRU 均可清理 |
| DT-010 | client / `duplicate_participator_update_returns_success` | 同 key 合法 Any 更新返回成功且内容替换 |
| DT-011 | participator / `allow_retry_does_not_enter_running` | check_prepare 返回 0+allow_retry；response 保留 reason，但 running/lock/timer/lifecycle callback 均不变化 |
| DT-012 | api/client/manager / `chrono_protocol_boundary_round_trip` | public C++ 参数为 chrono；0/负/亚秒/跨秒/大 duration；协议字段仍为 Timestamp/Duration；所有手写 seconds/nanos 路径被回归覆盖 |
| DT-013 | api/participator/manager / `timestamp_exact_nanosecond_carry` | 固定 now+duration 恰为 1e9ns；initialize、manager fallback、resolve 均得到 seconds+1/nanos=0，并覆盖负/多秒归一化 |
| DT-014 | client / `terminal_delivery_result_is_separate_and_bounded` | COMMITED/REJECTED 下通知 error→success、永久 error、多参与者顺序投递；事务返回只取协调者终态，每个参与者通知恰好最多 N 次，两个输出集合无旧值；终态通知投递失败不进入 `output_failed_participators`，仅 prepare 失败进入；外层协程被 kill/超时后不存在脱离 submit 生命周期的通知子任务，未投递参与者由自身 resolve timer 收敛 |
| DT-015 | participator / `resolve_budget_exhaustion_rejects_and_consumes` | `resolve_times == resolve_max_times` 时下一次进入直接走 reject、0 次 query；断言 running/lock/timer 转入有界 finished ack，最终本地消费且后续 tick 不再重试 |
| DT-016 | participator / `finished_ack_failure_has_budget` | ack 第 N 次成功和永久失败；interval 未到不发送，最多 N 次；耗尽后 finished/strong pointer 释放，后续 tick 不再发送，协调者由 TTL 兜底；resolve task 在拉起失败、`check_writable`=false、exiting、被 kill/超时（on_failed 兜底）时，未处理条目由 `rearm_unprocessed_timers` 按退避重新武装（`resolve_timepoint` 推后一个 `resolve_retry_interval`，幂等去重，不消耗 `resolve_times` 预算），下一次 tick 按退避恢复，既不永久丢失 timer 也不形成 tight loop |
| DT-017 | api / `chrono_duration_helper_returns_duration` | `static_assert` 返回 `google::protobuf::Duration`；0、正负亚秒、跨秒 round-trip；组件删除私有重复转换 helper |
| DT-018 | dtcoordsvr / `participant_ack_has_independent_terminal_state` | PREPARED/COMMITED/REJECTED 全局状态下分别发送两种 ack；全局状态不变，参与者非终态可进入请求方向；REJECTED→COMMITED 和 COMMITED→REJECTED 返回 `EN_TRANSACTION_FINISHED`；混合方向不删除、全部严格同向才删除 |
| DT-019 | dtcoordsvr / `timeout_reject_save_and_cache_invalidation` | 过期 PREPARED query 触发 REJECTED 和一次 CAS save；成功时 DB version 前进；`OLD_VERSION`、`NOT_FOUND` 和普通 DB/网络错误都返回错误并立即淘汰 cache；单次 save 不内部重试；四类调用入口只打印一次保存失败日志 |
| DT-020 | participator / `local_action_has_independent_retry_budget` | query 阶段已消耗 `resolve_times` 并合入 COMMITED/REJECTED 后进入本地动作；首次动作阶段将计数重置为 0，之后 RPC 重发/timer 重入跨驱动源累计且最多 N 次，不得被 query 预算提前消费或在阶段内反复清零 |
| DT-021 | participator / `terminal_ack_preserves_resolved_direction` | 副本响应未达 quorum 但已合入 COMMITED/REJECTED；事务进入 finished 后 ack timer 分别调用 commit_participator/reject_participator，不得因状态已经是最终态而跳过 RPC 并直接本地删除 |
| DT-022 | participator / `wound_intent_survives_reload_and_timer` | younger holder 被 older 事务抢占后 dump/load；快照保留 REJECTING，load 恢复 `wounded=true`；直接 commit 仍返回 `EN_TRANSACTION_RESOURCE_PREEMPTED` 且 0 次 Do，due timer 优先本地 reject 并进入同方向有界 ack，不得消费 timer 后永久留在 running |
| DT-023 | client / `force_commit_does_not_require_commit_callback` | force_commit 配置 prepare+reject callback 且 commit callback 为空：成功路径在 prepare 内完成并返回成功；失败路径仍可有界 undo；普通模式缺 commit callback 必须在入口返回 `EN_SYS_PARAM` |
| DT-024 | client / `legacy_constructor_and_layout_compatibility` | 直接构造、栈对象及 strong_rc_ptr 均可用；导出构造符号与上游兼容；不得为通知并发新增基类、passkey/factory 或改变对象布局；终态通知保持 submit 生命周期内顺序执行 |

实现后维护一份机器可读的 case 列表或在测试名中保留上述 ID；评审时逐行附 fresh command/result。任何未绿行都必须标成
blocked/failed，不能把整个 `distributed-transaction` label 的其他成功用例当作替代证据。

## 7. 资源泄漏与内存泄漏检查

### 7.1 每个功能用例的确定性资源断言

每个 case 结束前执行：

- `test.stop()==0`，并断言 `hard_timed_out=false`、没有未消费 SS/DB rule、没有 pending transport event；
- 释放所有 mock rule RAII handle 后再次调用同 RPC，确认旧 rule 不再命中；
- participant 的 running/finished 都为空，`dump()` 为空，每个测试资源的 `get_locker()==nullptr`；永久故障耗尽路径也必须
  满足，而不是只有成功路径满足；
- DB `get_all` 对已完成/删除事务返回 record-not-found；失败场景中应保留的记录仍存在；
- on_destroy 计数恰为 1；循环创建/销毁 handle 后，业务侧 strong reference 计数/weak sentinel 回到基线；
- 时间偏移、discovery 节点、config loader 和 singleton test seam 恢复，避免跨 case 污染。

增加 1,000 次短事务压力 case（不依赖 wall-clock sleep）：每轮 create/prepare/commit/ack/cleanup，周期性断言 DB 记录数、
participant map/lock 数、outbound history 和任务数不随轮次单调增长。该用例标 `stress`，不放入默认 `fast` 标签。

### 7.2 LSan/ASan 门禁

仓库已有 `PROJECT_SANTIZER_USE_ADDRESS` 和 `PROJECT_SANTIZER_USE_LEAK` 开关
（`project/cmake/ProjectBuildOption.cmake:64-68`）。当前 MSVC x64 checker 不接受仓库的 ASan 分支
（`project/cmake/ProjectSantizerChecker.cmake:66-89`），因此 Windows 常规测试不能替代 leak gate。

已执行通过：环境、命令与结果见第 0.8 节。

在 Linux Clang/GCC 的独立构建树执行：

```bash
cmake -S . \
  -B build_jobs_cmake_tools/_agent_tmp/distributed-transaction-asan \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_SHARED_LIBS=ON \
  -DPROJECT_ENABLE_UNITTEST=ON \
  -DPROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS=ON \
  -DPROJECT_SANTIZER_USE_ADDRESS=ON
cmake --build build_jobs_cmake_tools/_agent_tmp/distributed-transaction-asan \
  --target atf4g-co-component-distributed-transaction-api-unit-test \
           atf4g-co-component-distributed-transaction-client-unit-test \
           atf4g-co-component-distributed-transaction-participator-unit-test \
           atf4g-co-component-dtcoordsvr-unit-test \
  --parallel 12
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
ctest --test-dir build_jobs_cmake_tools/_agent_tmp/distributed-transaction-asan \
  -L distributed-transaction --output-on-failure
```

实际配置时还要复用该 Linux 构建环境已有的 toolchain/第三方参数；不能把当前 Windows cache 参数硬搬过去。Clang 官方文档
说明 LSan 可独立使用，也集成在 ASan 中；Linux 默认支持 leak detection，而其他平台不能一概视为支持：
[LeakSanitizer](https://clang.llvm.org/docs/LeakSanitizer.html)、
[AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html#memory-leak-detection)。

通过标准：0 个 direct/indirect leak、0 个 use-after-free/double-free/out-of-bounds、无 sanitizer suppression。
第三方库若有已知泄漏，先证明调用栈不属于本组件，再使用最窄 suppression，并单独记录，不能全局关闭 leak detection。

## 8. 执行顺序

1. **测试基础设施**：拆 target、复制 dtcoordsvr config loader、注册 DB message、增加必要的 test-only failure seam。
2. **纯函数/API**：merge、pack、initialize、data/participant mutation、lock/unlock、load/dump。
3. **已确认缺陷 RED**：按第 6 节逐 ID 建立最小失败用例，并确认失败原因正是对应控制流。
4. **SDK RPC 与 replication**：discovery、failover、R-of-N、OLD_VERSION、no-wait、malformed/timeout。
5. **协调者 service**：manager public API，再覆盖 7 个 action 和 response envelope。
6. **client/participant 状态机**：正常 2PC、force commit、rollback、snapshot restart、timer resolve、幂等/并发。
7. **资源门禁**：每 case teardown invariant、1,000 轮 stress、Linux ASan+LSan。

每一阶段遵循 RED-GREEN-REFACTOR：新增用例先证明旧代码按预期失败，再做最小修复，最后运行该 target、全部
`distributed-transaction` label 和全仓 `rpc-unit-test` 回归。

## 9. 建议运行命令与验收标准

Windows 当前构建树：

```powershell
cmake --build build_jobs_cmake_tools `
  --target atf4g-co-component-distributed-transaction-api-unit-test `
           atf4g-co-component-distributed-transaction-client-unit-test `
           atf4g-co-component-distributed-transaction-participator-unit-test `
           atf4g-co-component-dtcoordsvr-unit-test `
           atf4g-co-component-dtcoordsvr-stop-unit-test `
           atf4g-co-component-distributed-transaction-stress-unit-test `
  --parallel 12
ctest --test-dir build_jobs_cmake_tools `
  -L distributed-transaction `
  --output-on-failure
ctest --test-dir build_jobs_cmake_tools `
  -L rpc-unit-test `
  --output-on-failure
```

最终验收必须同时满足：

- 本文 4 节列出的每个 public API 至少有正常、参数错误和与其职责相关的故障用例；
- 第 6 节 DT-001..DT-024 每行都有独立 RED 用例，修复后转绿；若决定保留行为，必须补充明确的 public 契约而不是删除测试；
- 每个 runtime `stop()` 为 0，无 skip、无 hard timeout、无未消费 rule；
- Windows Debug 全部通过，Linux ASan+LSan 全部通过；
- 单 target、`-L distributed-transaction`、`-L rpc-unit-test` 三层回归均为 0 failure；
- 最终 diff 不包含生成文件手改、仓库根目录临时文件或与本组件无关的格式化。
