# atorbit 伪代码开发路线

## 目标

- atorbit 后续实现统一先写伪代码，再进入正式代码开发。
- 伪代码必须直接对应 [guide.md](guide.md) 的设计语义，不能脱离设计自行扩展协议或状态。
- 伪代码默认采用 C++ 风格写法，保留类和函数结构，并按 `.pseudo.h` / `.pseudo.cpp` 头文件与实现文件拆分。
- 伪代码与正式实现都要把 atorbit 视为 atframework 体系内组件，直接复用仓库中的 submodule。
- 每个伪代码文件只解决一个清晰问题，确保后续真实实现能按切片推进。

## atframework / x-server 对齐基线

- 子模块复用顺序固定为：`cmake-toolset`、`atframe_utils`、`libatbus`、`libatapp`。
- 伪代码设计时要先判断当前切片属于构建、通用工具、总线承载还是服务生命周期问题，再决定主要依赖哪个 atframework 组件。
- 风格严格对齐 x-server 约定：C++17、CMake 3.24.0+、`#pragma once`、snake_case、FWLOG 日志宏、匿名命名空间 + `static`、clang-format Google base / 120 列。
- 测试风格对齐 atframework 私有测试框架，真实测试应能翻译为 `CASE_TEST` 与 `CASE_EXPECT_*`。
- 正式代码目标结构对齐 atframework 组件惯例：`include/`、`src/`、`test/case/`、必要时配合 `project/`。

## 工程化对齐规则

- 服务 bootstrap 伪代码优先模拟 x-server 的 `main()` 装配方式：创建 `atfw::atapp::app`、加载 typed config、注册 dispatcher、注册 `module_impl`、最后 `app.run(...)`。
- 若某个模块属于服务生命周期或共享依赖装配，优先把它写成 `module_impl` 风格伪代码；若它属于业务请求处理，则优先写成 `dispatcher` 或 `logic/action` 风格文件，而不是塞进生命周期函数。
- 若某个流程只是“跨节点发送一个业务请求”，优先把它写成 RPC facade / dispatcher helper 调用，不要在业务伪代码中直接展开 `libatbus` 的底层 connection / channel 行为。
- 若某个流程依赖服务发现、节点选择或 metadata 过滤，优先在伪代码中标出 `etcd_module`、`service_discovery_index`、`etcd_discovery_set` 的使用点。
- 面向客户端或下游服务的 RPC 伪代码，默认要能映射到 `handle_cs_rpc_*` / `handle_ss_rpc_*`、`task_action_*`、`rpc/<module>/<service>` 三层文件结构。
- 伪代码中如果出现 DB 访问、全局 ID、跨服务调用，应优先写成 `rpc::db::*` 或 `rpc::<module>::<service>::*` facade 调用，而不是直接写底层存储或原始总线包处理。
- 与 transport 或节点事件强相关的流程，要明确区分“业务 ACK / 重试 / 回放”和“node up/down / reconnect / ready 事件”，避免把两层责任混在同一函数。

## 强制规范

### 1. 函数级展开

- 每个核心流程必须展开到函数签名级别，而不是只写流程图或自然语言步骤。
- 每个函数体必须包含关键语句级伪代码，包括变量定义、分支、循环、返回值、资源回滚和日志。
- 每个调用的函数、发送的命令、状态变更、计时器操作和 ACK 操作都要显式标出。

### 2. 文件级拆分

- 伪代码实现文件统一使用 `.pseudo.h` / `.pseudo.cpp`；模块主体建议命名为 `<module>.pseudo.h` 与 `<module>.pseudo.cpp`，测试文档建议命名为 `<module>_test.pseudo.cpp`。
- `README.md` 仍然只作为索引容器；真正的实现伪代码写在 `.pseudo.*` 文件中，不能退化为纯说明文。
- 同一个模块至少要体现头文件声明、实现文件和测试文件三个层次；复杂模块可以再拆辅助流程文件。
- 若目标是 atframework 组件式 C++ 实现，伪代码应优先模拟未来 `include/`、`src/`、`test/case/` 的分层，而不只是业务目录名。
- 若目标是 x-server 风格的服务实现，伪代码还应显式标出它未来会落到 `app/`、`dispatcher/`、`logic/action/`、`router/`、`rpc/` 中的哪一层。

### 3. 类与函数结构，按 C++ 头源拆分

- 允许使用接近 C++ 的概念名称和类型，例如 `bool`、`int64_t`、`std::string`、`std::vector`、`std::unordered_map`、`connection_handle_t`。
- 推荐在 `.pseudo.h` 中放类、结构体、成员、函数签名和依赖注释；在 `.pseudo.cpp` 中放函数定义、局部辅助函数和主流程伪代码。
- 函数体内部可以使用中文注释或中文步骤语句，简化不重要的实现细节，但整体外壳保持 C++ 风格。
- 可以使用 `#pragma once`、命名空间、访问修饰符、引用/指针、返回值类型和局部静态函数等 C++ 结构。
- 不要求完全可编译，但要求结构完整到可以直接翻译成真实代码。

### 4. 协议与伪代码的边界

- 协议字段、消息编号、枚举和 oneof 关系统一使用 Protobuf 表达，不再用伪代码重复定义。
- 伪代码只描述如何构造、发送、解析和处理 Protobuf 消息。

### 5. 测试也写伪代码

- 每个核心伪代码文档都应该附带测试用例伪代码。
- 测试伪代码至少包含 Arrange、Act、Assert。
- 测试中要显式写出 Mock 行为、时间推进、消息输入、期望输出和指标校验。

## 目录约定

```text
doc/orbit/pseudocode/
  README.md
  shared/
    runtime/
    metrics/
  dsa/
    agent/
    process/
    heartbeat/
  dsc/
    registry/
    scheduler/
    session/
    forwarding/
  sdk/
    external/
    ds/
  dsm/
    service/
    topology/
    control/
```

```text
proto/
  agent.proto
  controller.proto
  external.proto
  manager.proto
```

```text
future-code-layout/
  include/
  src/
  test/
    case/
  project/
```

## 文件形态示例

```text
doc/orbit/pseudocode/dsc/scheduler/
  scheduler_service.pseudo.h
  scheduler_service.pseudo.cpp
  scheduler_service_test.pseudo.cpp
```

```text
future-code-layout/
  include/atorbit/dsc/scheduler_service.h
  src/dsc/scheduler_service.cpp
  test/case/scheduler_service_test.cpp
```

## 推荐首批伪代码切片

1. `doc\orbit\proto\*.proto` DSA / DSC / DSM / SDK 四条链路的正式协议定义。
2. `doc/orbit/pseudocode/shared/runtime/resource_ledger.pseudo.h` + `resource_ledger.pseudo.cpp`: CPU/内存账本、预扣与修正规则。
3. `doc/orbit/pseudocode/dsa/process/start_ds_process.pseudo.h` + `start_ds_process.pseudo.cpp`: DSA 拉起 DS 的完整流程。
4. `doc/orbit/pseudocode/dsa/heartbeat/heartbeat_monitor.pseudo.h` + `heartbeat_monitor.pseudo.cpp`: 心跳、超时、Crash、正常退出和 OOM Kill。
5. `doc/orbit/pseudocode/dsc/scheduler/scheduler_service.pseudo.h` + `scheduler_service.pseudo.cpp`: DSA 选择、预分发和 in-flight 回收。
6. `doc/orbit/pseudocode/dsc/session/session_router.pseudo.h` + `session_router.pseudo.cpp`: Unique ID、复合 DS ID、权限校验和断线处理。
7. `doc/orbit/pseudocode/dsc/forwarding/reliable_forwarder.pseudo.h` + `reliable_forwarder.pseudo.cpp`: ACK、重传、离线缓冲和重放。
8. `doc/orbit/pseudocode/sdk/external/external_client.pseudo.h`、`external_client.pseudo.cpp` 与 `doc/orbit/pseudocode/sdk/ds/ds_client.pseudo.h`、`ds_client.pseudo.cpp`: 两侧 SDK 的接口与回调语义。
9. `doc/orbit/pseudocode/dsm/topology/cluster_catalog.pseudo.h` + `cluster_catalog.pseudo.cpp`: DSM 侧 Region / Controller / Agent / DS inventory 聚合。
10. `doc/orbit/pseudocode/dsm/service/rpc/managerservice.pseudo.h`、`managerservice.pseudo.cpp` 与 `doc/orbit/pseudocode/dsm/control/maintenance_coordinator.pseudo.h`、`maintenance_coordinator.pseudo.cpp`: DSM 的查询、drain、stop、routing plan 控制链路。

## 单个伪代码模块建议模板

```text
module_x/
  module_x.pseudo.h
  module_x.pseudo.cpp
  module_x_test.pseudo.cpp
```

### 模块模板

```text
# module_x.pseudo.h

#pragma once

class module_x {
public:
  result_code_t handle_event(const event_t& event);

private:
  state_table_t state_table_;
  bool validate_event(const event_t& event) const;
  state_record_t* find_state(int64_t id);
};

# module_x.pseudo.cpp

result_code_t module_x::handle_event(const event_t& event) {
  if (!validate_event(event)) {
    // 记录非法事件并直接返回失败
    return result_code_t::invalid_argument;
  }

  auto* state = find_state(event.id());
  if (nullptr == state) {
    // 找不到状态时返回未找到
    return result_code_t::not_found;
  }

  // 更新状态并发送后续命令
  apply_event_to_state(*state, event);
  send_followup_command(*state);
  return result_code_t::ok;
}
```

### 测试文件模板

```text
# module_x_test.pseudo.cpp

CASE_TEST(module_x, handle_event_returns_state_not_found_when_state_missing) {
  module_x component;
  auto event = make_event(100);

  auto result = component.handle_event(event);

  CASE_EXPECT_EQ(result_code_t::not_found, result);
}
```

## 开发流程

1. 先阅读 [guide.md](guide.md) 的对应章节；如果目标切片属于 DSM，再同步阅读 [dsm-guide.md](dsm-guide.md)。
2. 再看 [implementation-plan.md](implementation-plan.md)，选择当前阶段内最小切片。
3. 先判断该切片主要依赖哪个 atframework 子模块，并确认未来真实代码要落到 `include/`、`src/`、`test/case/` 的哪个位置。
4. 再判断它在 x-server 风格下属于 `module_impl`、`dispatcher helper`、`task_action`、`router` 还是 `rpc facade`，避免责任边界混乱。
5. 再决定目标模块的目录层级和文件拆分，至少确定 `.pseudo.h`、`.pseudo.cpp` 和 `_test.pseudo.cpp`。
6. 将切片放入最接近责任边界的 `doc/orbit/pseudocode/` 子目录。
7. 先写模块 `.pseudo.h` 中的类与状态结构，再写 `.pseudo.cpp` 中的函数体和辅助函数。
8. 同步补上 `_test.pseudo.cpp`，覆盖成功路径、失败路径和超时路径，并尽量对齐 `CASE_TEST` 风格。
9. 如果发现设计缺口，先回写到设计文档（`guide.md` 或 `dsm-guide.md`）再继续伪代码，不要继续保留“仅占位”的 DSM 模块。
10. 只有当伪代码被确认后，才进入正式代码实现。

## 命名建议

- 文件名尽量采用真实模块名与职责名，例如 `scheduler_service.pseudo.h`、`scheduler_service.pseudo.cpp`、`reliable_forwarder_test.pseudo.cpp`。
- 一个目录对应一个清晰模块；跨组件链路可以拆成多个模块文件，不要把多个组件内部实现混在同一个文件里。
- 函数名尽量采用动词短语，例如 `ReserveInflightSlot`、`ReplayBufferedMessages`、`HandleHeartbeatTimeout`。
- 真实代码文件命名也应对齐 x-server / atframework 组件命名习惯，避免出现驼峰式模块文件名。
- DSM 目录按 `service/`、`topology/`、`control/` 展开，优先表达 inventory、maintenance、routing plan 与审计等控制平面职责。