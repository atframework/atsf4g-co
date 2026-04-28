# atorbit 实现计划

> 本文件描述 atorbit 中 DSA/DSC/DSM 相关能力的实现推进顺序。
>
> 架构、协议和状态设计以 [guide.md](guide.md) 为准；后续开发默认先落函数级伪代码，再进入正式代码实现。

## 总体原则

- 设计先于实现，任何实现切片都要先回到 [guide.md](guide.md) 对齐语义。
- 伪代码先于正式代码，先在 `pseudocode/` 固化函数、状态、异常分支和测试用例。
- atorbit 作为 atframework 体系内组件，正式实现必须优先复用仓库内 `atframework/` 下的 submodule，而不是重新发明基础设施。
- 组件复用顺序固定为：`cmake-toolset` 负责构建链路，`atframe_utils` 负责通用工具与测试框架，`libatbus` 负责总线与协议承载，`libatapp` 负责服务生命周期、配置、etcd 与连接器集成。
- 工程风格、目录组织、测试方式和日志规范严格对齐 x-server 仓库中的 atframework 组件实践。
- 伪代码实现文件统一使用 `.pseudo.h` / `.pseudo.cpp`；这些文件必须按模块组织，并模拟未来正式工程的目录层级。
- 伪代码保留类、状态、函数、测试结构，整体写法向 C++ 靠拢，按头文件 / 实现文件拆分；函数签名和类型命名尽量贴近未来真实代码。
- 模块主体建议使用 `<module>.pseudo.h` 与 `<module>.pseudo.cpp`，测试建议使用 `<module>_test.pseudo.cpp`；必要时再拆多个职责单一的伪代码文件。
- 函数内部逻辑不能用自然语言整段代替，必须写出可追踪的伪代码语句；被调用函数、发送指令、状态更新、资源变更、ACK 处理都要显式写出。
- 协议定义不使用伪代码，统一使用 `.proto` 文件表达消息结构、字段编号、枚举和 oneof 关系。
- 构建流程统一使用 CMake，负责 Protobuf 代码生成、测试目标、Mock 目标和后续工程集成。
- 真实代码布局默认对齐 atframework 组件习惯：公共头文件进入 `include/`，实现进入 `src/`，测试进入 `test/case/`，必要的 CMake 辅助逻辑进入 `project/`。
- 测试框架默认对齐 `atframe_utils` / `libatbus` / `libatapp` 私有测试框架，测试设计要能映射为 `CASE_TEST` 与 `CASE_EXPECT_*` 风格。
- 日志与错误处理默认对齐 atframework 组件习惯：日志使用 `FWLOG*` 宏，错误通过返回码、错误枚举或兼容 `nostd::result` / 状态对象的方式表达。
- 每次只推进一个最小切片，避免同时跨 DSA、DSC、SDK 多面扩散。
- DSM 作为全局管理平面独立建模：业务热路径仍走 DSA / DSC / SDK，跨 Region inventory、drain / stop / reconcile 与运维审计统一进入 DSM。

## atframework 组件使用基线

| 组件 | 在 atorbit 中的职责 | 使用约束 |
|------|---------------------|----------|
| `atframework/cmake-toolset` | 工具链、第三方依赖、Protobuf 代码生成、测试目标 | 不自行重新设计构建链路，优先沿用现有 toolchain 与 CMake 习惯 |
| `atframework/atframe_utils` | 通用工具、结果类型、日志辅助、测试框架 | 通用能力优先复用，不重复造基础容器、通用字符串/时间/内存工具 |
| `atframework/libatbus` | 消息总线、路由、链路协议、底层转发承载 | DSA/DSC/外部服务间的总线语义优先基于其抽象建模 |
| `atframework/libatapp` | 服务生命周期、配置加载、模块系统、etcd、连接器 | DSA/DSC 服务骨架与配置/服务发现流程优先靠齐其模块化用法 |

## x-server 风格基线

- C++ 标准：C++17 required。
- 构建系统：CMake 3.24.0+。
- 头文件：统一使用 `#pragma once`。
- 命名：类/结构体/函数使用 `snake_case`，常量使用 `UPPER_SNAKE_CASE`，共享类型别名在需要时使用 `*_t` 后缀，成员变量优先使用 `snake_case_`。
- 日志：统一使用 `FWLOGINFO`、`FWLOGWARNING`、`FWLOGERROR` 等 FWLOG 宏族。
- 文件内局部函数：放在匿名命名空间中，同时保留 `static` 关键字。
- 代码格式化：对齐仓库根 `.clang-format`，Google base，column limit 120。
- 单元测试：对齐 atframework 私有测试框架，真实代码阶段进入 `test/case/`。

## x-server 工程落地补充

- 服务入口伪代码默认对齐 `atfw::atapp::app + main_service_module + dispatcher modules` 的装配模式，避免写成单体式 `RunController()`。
- `module_impl` 生命周期只负责装配、共享依赖初始化、reload 与 cleanup；核心业务流程应下沉到 `dispatcher/`、`logic/action/`、`router/` 等层。
- 服务发现与在线节点管理优先复用 `etcd_module`、`service_discovery_index`、`etcd_discovery_set`；DSA / DSC 的自定义 registry 只表达业务资源状态，不替代在线发现源。
- `libatbus` 在 plan 中应视为底层承载与路由组件，业务伪代码不要直接展开 socket / channel 细节，也不要在业务层直接堆 `send_data(...)`。
- RPC 伪代码必须保持 x-server 的文件拆分：`rpc/<module>/<service>` 表达调用 facade，`handle_cs_rpc_*` / `handle_ss_rpc_*` 表达注册入口，`task_action_*` 表达业务动作。
- 每个 RPC 业务动作都应优先映射到 `rpc::context`、`rpc::shared_message<T>`、`RPC_AWAIT_CODE_RESULT(...)`、`RPC_RETURN_CODE(...)`、`TASK_ACTION_RETURN_CODE(...)` 这类返回码模型。
- 需要驱动路由切换、网关指令、会话下发时，优先经 dispatcher / helper / facade 层表达，不要把 transport 发送逻辑直接内联到核心业务函数。
- 依赖 discovery、reconnect、node up/down 的切片，要在计划中同时写出测试维度，至少覆盖节点上下线、重连、策略选择和失败回滚。

## proto 与未来 RPC 文件树基线

- 以下 service 名称和文件树作为当前 plan 的默认命名锚点；后续如果 [guide.md](guide.md) 明确更细的领域划分，可以在不改变分层与职责边界的前提下调整名字。
- Protobuf 是协议唯一源定义；`pb.h / pb.cc` 属于协议生成物，`rpc/<module>/<service>`、`handle_*`、`task_action_*` 属于未来真实代码树中的 facade、wiring 与业务动作层。
- 先固定 proto 边界，再固定未来 RPC 文件树，再把每个核心流程拆成 `service -> handle -> task_action -> helper/facade` 路径，避免后续伪代码继续漂在抽象层。

### proto 推荐拆分

| 文件 | 责任 | 首批关注内容 |
|------|------|--------------|
| `src/shared/protocol/protocol/common/common.proto` | 共享标识、元数据、ACK、错误码、序列与路由公共结构 | `request_meta`、`ack_meta`、`agent_identity`、`ds_identity`、`session_identity`、`load_snapshot`、公共错误枚举 |
| `src/dsa/protocol/protocol/pbdesc/agent.proto` | DSC → DSA 的命令型 RPC | `service AgentService`，首批方法覆盖 DS 生命周期、下行转发与上行 ACK |
| `src/dsc/protocol/protocol/pbdesc/controller.proto` | DSA → DSC 的注册、心跳、负载、DS 事件与上行转发 RPC | `service ControllerService`，首批方法覆盖 DSA 注册、状态同步、DS 启停事件、上行消息与下行 ACK |
| `src/dsc/protocol/protocol/pbdesc/external.proto` | 外部服务 / SDK → DSC 的请求 RPC 以及 DSC → 外部的通知契约 | `service ExternalService`，首批方法覆盖连接、重连、拉起、下行发包、上行 ACK 与会话清理，并预留下行通知消息 |
| `proto/manager.proto` | DSM ↔ DSC / 平台侧的管理面 RPC | `service ManagerService`，首批方法覆盖 controller 注册、inventory 汇总、stop ds、drain controller / region 与 routing plan |

### AgentService 首批方法清单

| 方法 | 请求 / 响应 | 设计来源 | 未来落点 |
|------|-------------|----------|----------|
| `StartDS` | `StartDSReq` / `StartDSRsp` | DSC 选择 DSA 后下发拉起命令，对应 guide 中 DSA 拉起 DS 主流程 | `handle_ss_rpc_agentservice` → `task_action_start_ds` |
| `StopDS` | `StopDSReq` / `StopDSRsp` | 运营回收、异常清理或 drain 过程中的显式停止 | `handle_ss_rpc_agentservice` → `task_action_stop_ds` |
| `QueryDSState` | `QueryDSStateReq` / `QueryDSStateRsp` | 查询单个 DS、批量 DS 或 agent 级运行快照 | `handle_ss_rpc_agentservice` → `task_action_query_ds_state` |
| `DrainAgent` | `DrainAgentReq` / `DrainAgentRsp` | 把 DSA 切入不可继续拉起新 DS 的 drain 模式 | `handle_ss_rpc_agentservice` → `task_action_drain_agent` |
| `ForwardToDS` | `ForwardToDSReq` / `ForwardToDSRsp` | 外部服务到 DS 的管理面下行消息，经 DSC 转发给 DSA 后再送入 DS | `handle_ss_rpc_agentservice` → `task_action_forward_to_ds` |
| `AckUpstream` | `AckUpstreamReq` / `AckUpstreamRsp` | DSC / 外部服务确认已消费 DS 上行消息，对应 guide 中上行 ACK 回流 | `handle_ss_rpc_agentservice` → `task_action_ack_upstream` |

### ControllerService 首批方法清单

| 方法 | 请求 / 响应 | 设计来源 | 未来落点 |
|------|-------------|----------|----------|
| `RegisterAgent` | `RegisterAgentReq` / `RegisterAgentRsp` | DSA 建连后上报容量、Region、当前 DS 列表，对应 guide 中 DSA 注册 | `handle_ss_rpc_controllerservice` → `task_action_register_agent` |
| `HeartbeatAgent` | `HeartbeatAgentReq` / `HeartbeatAgentRsp` | 连接存活与轻量状态续约，避免仅依赖长周期负载上报 | `handle_ss_rpc_controllerservice` → `task_action_heartbeat_agent` |
| `ReportAgentLoad` | `ReportAgentLoadReq` / `ReportAgentLoadRsp` | DSA 定期或变化时上报 CPU、内存、DS 数量与可用容量 | `handle_ss_rpc_controllerservice` → `task_action_report_agent_load` |
| `NotifyDSStarted` | `NotifyDSStartedReq` / `NotifyDSStartedRsp` | DS 子进程启动成功后，DSA 回报 DS 标识、地址与 request_id，对应 guide 中 DS 启动完成 | `handle_ss_rpc_controllerservice` → `task_action_notify_ds_started` |
| `NotifyDSExit` | `NotifyDSExitReq` / `NotifyDSExitRsp` | DS 正常退出、Crash、心跳超时或 OOM Kill 后的统一退出上报 | `handle_ss_rpc_controllerservice` → `task_action_notify_ds_exit` |
| `ForwardFromDS` | `ForwardFromDSReq` / `ForwardFromDSRsp` | DS 通过 DSA 向外部服务发送上行管理消息，对应 guide 中转发消息 | `handle_ss_rpc_controllerservice` → `task_action_forward_from_ds` |
| `AckDownstream` | `AckDownstreamReq` / `AckDownstreamRsp` | DSA / DS 确认收到下行消息，对应 guide 中下行消息的 ACK | `handle_ss_rpc_controllerservice` → `task_action_ack_downstream` |
| `ReconcileAgentState` | `ReconcileAgentStateReq` / `ReconcileAgentStateRsp` | DSA 启动恢复、异常重连或 DSC 重建状态时的全量快照同步 | `handle_ss_rpc_controllerservice` → `task_action_reconcile_agent_state` |

### ExternalService 首批方法清单

| 方法 | 请求 / 响应 | 设计来源 | 未来落点 |
|------|-------------|----------|----------|
| `ConnectExternal` | `ConnectExternalReq` / `ConnectExternalRsp` | 外部服务首次连接 DSC，登记 Unique ID 与基础会话元数据 | `handle_cs_rpc_externalservice` → `task_action_connect_external` |
| `ReconnectExternal` | `ReconnectExternalReq` / `ReconnectExternalRsp` | 外部服务携带 `last_received_seq` 重连，触发回放窗口恢复 | `handle_cs_rpc_externalservice` → `task_action_reconnect_external` |
| `LaunchDedicatedServer` | `LaunchDedicatedServerReq` / `LaunchDedicatedServerRsp` | 外部服务请求 DSC 调度并拉起新的 DS | `handle_cs_rpc_externalservice` → `task_action_launch_dedicated_server` |
| `SendToDS` | `SendToDSReq` / `SendToDSRsp` | 外部服务向指定 DSA / DS 发送下行管理面消息 | `handle_cs_rpc_externalservice` → `task_action_send_to_ds` |
| `AckUpstream` | `AckUpstreamReq` / `AckUpstreamRsp` | 外部服务确认已收到 DSC 转发的 DS 上行消息，用于释放缓冲与重放游标 | `handle_cs_rpc_externalservice` → `task_action_ack_upstream` |
| `RemoveSession` | `RemoveSessionReq` / `RemoveSessionRsp` | 外部服务主动移除 Unique ID 会话、解除路由与 DS 绑定 | `handle_cs_rpc_externalservice` → `task_action_remove_session` |

### ExternalService 下行通知消息

| 消息 | 方向 | 责任 |
|------|------|------|
| `ForwardFromDSNotify` | DSC → 外部服务 | 把 DS 上行消息转发给外部服务 |
| `DownstreamAckNotify` | DSC → 外部服务 | 确认某条 `SendToDS` 下行消息已经被 DSA / DS 接收 |
| `DSExitNotify` | DSC → 外部服务 | 告知 DS 已退出及退出原因、退出码和 user_data |

### ManagerService 首批方法清单

| 方法 | 请求 / 响应 | 设计来源 | 未来落点 |
|------|-------------|----------|----------|
| `RegisterController` | `RegisterControllerReq` / `RegisterControllerRsp` | DSC 启动后向 DSM 注册自身 Region、能力和控制属性 | `handle_ss_rpc_managerservice` → `task_action_register_controller` |
| `ReportControllerSnapshot` | `ReportControllerSnapshotReq` / `ReportControllerSnapshotRsp` | DSC 周期向 DSM 汇总 agent / ds / session / inflight 快照 | `handle_ss_rpc_managerservice` → `task_action_report_controller_snapshot` |
| `QueryInventory` | `QueryInventoryReq` / `QueryInventoryRsp` | 平台 / 运维查询 Region、DSC、DSA、DS 全局 inventory | `handle_ss_rpc_managerservice` → `task_action_query_inventory` |
| `StopDedicatedServer` | `StopDedicatedServerReq` / `StopDedicatedServerRsp` | DSM 通过 DSC 下发 stop 指令，最终回收到目标 DS | `handle_ss_rpc_managerservice` → `task_action_stop_dedicated_server` |
| `DrainController` | `DrainControllerReq` / `DrainControllerRsp` | 把单个 DSC 切入 drain 状态，停止新 LaunchDS 与新会话 | `handle_ss_rpc_managerservice` → `task_action_drain_controller` |
| `DrainRegion` | `DrainRegionReq` / `DrainRegionRsp` | 对整个 Region 批量下发 drain 策略与维护窗口 | `handle_ss_rpc_managerservice` → `task_action_drain_region` |
| `ApplyRoutingPlan` | `ApplyRoutingPlanReq` / `ApplyRoutingPlanRsp` | 切换 Route Plan、黑名单 Controller、维护优先级 | `handle_ss_rpc_managerservice` → `task_action_apply_routing_plan` |

### 未来代码树基线

```text
build/_generated/proto/
	common.pb.h
	common.pb.cc
	agent.pb.h
	agent.pb.cc
	controller.pb.h
	controller.pb.cc
	external.pb.h
	external.pb.cc
	manager.pb.h
	manager.pb.cc

src/dsa/service/
	app/
		dsa_main.cpp
		handle_ss_rpc_agentservice.h
		handle_ss_rpc_agentservice.cpp
	logic/action/
		task_action_start_ds.h
		task_action_start_ds.cpp
		task_action_stop_ds.h
		task_action_stop_ds.cpp
		task_action_query_ds_state.h
		task_action_query_ds_state.cpp
		task_action_drain_agent.h
		task_action_drain_agent.cpp
		task_action_forward_to_ds.h
		task_action_forward_to_ds.cpp
		task_action_ack_upstream.h
		task_action_ack_upstream.cpp
	rpc/agent/
		agentservice.h
		agentservice.cpp

src/dsc/service/
	app/
		dsc_main.cpp
		handle_ss_rpc_controllerservice.h
		handle_ss_rpc_controllerservice.cpp
		handle_cs_rpc_externalservice.h
		handle_cs_rpc_externalservice.cpp
	logic/action/
		task_action_register_agent.h
		task_action_register_agent.cpp
		task_action_heartbeat_agent.h
		task_action_heartbeat_agent.cpp
		task_action_report_agent_load.h
		task_action_report_agent_load.cpp
		task_action_notify_ds_started.h
		task_action_notify_ds_started.cpp
		task_action_notify_ds_exit.h
		task_action_notify_ds_exit.cpp
		task_action_forward_from_ds.h
		task_action_forward_from_ds.cpp
		task_action_ack_downstream.h
		task_action_ack_downstream.cpp
		task_action_reconcile_agent_state.h
		task_action_reconcile_agent_state.cpp
		task_action_connect_external.h
		task_action_connect_external.cpp
		task_action_reconnect_external.h
		task_action_reconnect_external.cpp
		task_action_launch_dedicated_server.h
		task_action_launch_dedicated_server.cpp
		task_action_send_to_ds.h
		task_action_send_to_ds.cpp
		task_action_ack_upstream.h
		task_action_ack_upstream.cpp
		task_action_remove_session.h
		task_action_remove_session.cpp
	rpc/controller/
		controllerservice.h
		controllerservice.cpp
	rpc/external/
		externalservice.h
		externalservice.cpp

src/dsm/service/
	app/
		dsm_main.cpp
		handle_ss_rpc_managerservice.h
		handle_ss_rpc_managerservice.cpp
	logic/action/
		task_action_register_controller.h
		task_action_register_controller.cpp
		task_action_report_controller_snapshot.h
		task_action_report_controller_snapshot.cpp
		task_action_query_inventory.h
		task_action_query_inventory.cpp
		task_action_stop_dedicated_server.h
		task_action_stop_dedicated_server.cpp
		task_action_drain_controller.h
		task_action_drain_controller.cpp
		task_action_drain_region.h
		task_action_drain_region.cpp
		task_action_apply_routing_plan.h
		task_action_apply_routing_plan.cpp
	rpc/manager/
		managerservice.h
		managerservice.cpp

src/dsm/topology/
	cluster_catalog.h
	cluster_catalog.cpp

src/dsm/control/
	maintenance_coordinator.h
	maintenance_coordinator.cpp
	routing_plan_service.h
	routing_plan_service.cpp

test/case/
	dsa_agentservice_test.cpp
	dsc_controllerservice_test.cpp
	dsc_externalservice_test.cpp
	dsm_managerservice_test.cpp
```

### 核心流程默认路径

| 核心流程 | service | handle | task_action / 入口动作 | 主要 helper / facade |
|----------|---------|--------|------------------------|----------------------|
| DSC 下发拉起 DS | `AgentService::StartDS` | `handle_ss_rpc_agentservice` | `task_action_start_ds` | `start_ds_process`、`resource_ledger`、`local_channel_service` |
| DSC 下发停止 DS | `AgentService::StopDS` | `handle_ss_rpc_agentservice` | `task_action_stop_ds` | `process_supervisor`、`heartbeat_monitor` |
| DSC 下发 drain | `AgentService::DrainAgent` | `handle_ss_rpc_agentservice` | `task_action_drain_agent` | `resource_ledger`、`process_supervisor` |
| DSC 下发管理面消息到 DS | `AgentService::ForwardToDS` | `handle_ss_rpc_agentservice` | `task_action_forward_to_ds` | `local_channel_service`、`pending_ack_queue` |
| DSC 确认 DS 上行消息已消费 | `AgentService::AckUpstream` | `handle_ss_rpc_agentservice` | `task_action_ack_upstream` | `local_channel_service`、`reconnect_replay` |
| DSA 上报注册 | `ControllerService::RegisterAgent` | `handle_ss_rpc_controllerservice` | `task_action_register_agent` | `agent_registry`、`service_discovery_bridge` |
| DSA 上报心跳 / 负载 | `ControllerService::HeartbeatAgent` / `ReportAgentLoad` | `handle_ss_rpc_controllerservice` | `task_action_heartbeat_agent` / `task_action_report_agent_load` | `agent_registry`、`scheduler_service` |
| DSA 上报 DS 启动完成 | `ControllerService::NotifyDSStarted` | `handle_ss_rpc_controllerservice` | `task_action_notify_ds_started` | `scheduler_service`、`session_router` |
| DSA 上报 DS 退出 | `ControllerService::NotifyDSExit` | `handle_ss_rpc_controllerservice` | `task_action_notify_ds_exit` | `disconnect_cleanup`、`session_router` |
| DSA 上报 DS 上行消息 | `ControllerService::ForwardFromDS` | `handle_ss_rpc_controllerservice` | `task_action_forward_from_ds` | `reliable_forwarder`、`upstream_buffer_store` |
| DSA 上报下行 ACK | `ControllerService::AckDownstream` | `handle_ss_rpc_controllerservice` | `task_action_ack_downstream` | `pending_ack_queue`、`reliable_forwarder` |
| 外部请求拉起 DS | `ExternalService::LaunchDedicatedServer` | `handle_cs_rpc_externalservice` | `task_action_launch_dedicated_server` | `scheduler_service`、`rpc::agent::agentservice` |
| 外部请求下行发包 | `ExternalService::SendToDS` | `handle_cs_rpc_externalservice` | `task_action_send_to_ds` | `reliable_forwarder`、`session_router` |
| 外部连接 / 重连 | `ExternalService::ConnectExternal` / `ReconnectExternal` | `handle_cs_rpc_externalservice` | `task_action_connect_external` / `task_action_reconnect_external` | `session_router`、`reconnect_replay` |
| 外部确认上行消息 | `ExternalService::AckUpstream` | `handle_cs_rpc_externalservice` | `task_action_ack_upstream` | `upstream_buffer_store`、`reconnect_replay` |
| 外部移除会话 | `ExternalService::RemoveSession` | `handle_cs_rpc_externalservice` | `task_action_remove_session` | `session_router`、`disconnect_cleanup` |
| DSM 汇总 controller inventory | `ManagerService::ReportControllerSnapshot` | `handle_ss_rpc_managerservice` | `task_action_report_controller_snapshot` | `cluster_catalog`、`routing_plan_service` |
| DSM 主动停止某个 DS | `ManagerService::StopDedicatedServer` | `handle_ss_rpc_managerservice` | `task_action_stop_dedicated_server` | `maintenance_coordinator`、`rpc::agent::agentservice` |
| DSM 下发 controller / region drain | `ManagerService::DrainController` / `DrainRegion` | `handle_ss_rpc_managerservice` | `task_action_drain_controller` / `task_action_drain_region` | `maintenance_coordinator`、`cluster_catalog` |

## 伪代码强制规范

### 1. 书写层级

- 文档级：说明目标、上下文、约束、输入输出和依赖关系。
- 模块级：说明核心数据结构、状态机、线程或协程边界。
- 函数级：每个主流程必须展开到函数签名级别。
- 语句级：函数体内必须写出关键语句，如 `IF`、`FOR EACH`、`RETURN`、`CALL send_message`、`CALL acquire_lock`、`CALL start_timer`。
- 工程级：说明该模块最终将落到 `include/`、`src/`、`test/case/` 中的哪个位置，以及它依赖哪个 atframework 子组件。

### 2. 伪代码风格

- 使用接近 C++ 的类型、控制流和命名方式，例如 `bool`、`int64_t`、`std::string`、`std::vector`、`if`、`for`、`return`、`auto`。
- 可以保留抽象句柄或未完全展开的类型，但必须可映射到真实代码，例如 `connection_handle_t`、`timer_handle_t`、`process_handle_t`。
- 函数名使用动词短语，例如 `select_agent_for_launch`、`handle_heartbeat_timeout`、`replay_buffered_messages`。
- 错误处理显式化，统一写出失败返回、错误码、日志和资源回滚。
- 若伪代码准备映射到 atframework 真实组件，需显式标出其与 `libatapp`、`libatbus`、`atframe_utils` 的交互点。
- 头文件使用 `#pragma once`，用于声明类、结构体、成员和函数签名；实现文件使用命名空间、局部静态函数和 C++ 风格函数体。
- 函数体内部可以用中文步骤注释或中文伪代码语句简化实现细节，但外层结构保持 C++ 头文件 / 源文件风格。

### 3. 示例模板

```text
#pragma once

class controller_service {
public:
	launch_result_t launch_dedicated_server(const launch_request_t& request);
};

launch_result_t controller_service::launch_dedicated_server(const launch_request_t& request) {
	auto validation = validate_launch_request(request);
	if (!validation.ok()) {
		// 记录请求不合法原因并直接返回失败
		return launch_result_t::failed(validation.error_code());
	}

	auto candidate = select_agent_for_launch(request.region(), request.expected_cpu(), request.expected_memory());
	if (!candidate) {
		// 记录无可用 agent，并返回容量不足
		return launch_result_t::failed(error_code_t::k_no_available_agent);
	}

	reserve_inflight_slot(candidate->agent_id, request.request_id());
	send_start_ds_request(candidate->connection, request);
	start_inflight_timer(request.request_id(), inflight_timeout_ms_);
	return launch_result_t::pending(request.request_id());
}
```

### 4. 测试伪代码要求

- 测试也采用函数级伪代码，至少包括 Arrange、Act、Assert 三段。
- 测试中要显式写出 Mock 行为、输入消息、超时推进、期望回包、指标变化和资源变化。
- 协议编解码测试应直接引用 `.proto` 消息和字段，不再用口头描述替代。
- 伪代码测试文件统一使用 `<module>_test.pseudo.cpp`，正文直接采用 `CASE_TEST`、`CASE_EXPECT_*` 风格书写。
- 下文 phase 中保留的 `CASE_TEST` 风格代码块，统一视为 `.pseudo.cpp` 推荐正文写法，而不是单独的映射占位。

## Phase 1: 协议、构建与骨架

### 目标

- 固化正式协议定义、CMake 构建入口和共享基础结构。
- 建立第一批函数级伪代码文档，明确后续实现边界。
- 建立与 atframework submodule 的真实集成边界，避免后续实现偏离 x-server 风格。
- 当前阅读版 Phase 1 只落 `.proto` 描述文件和 `.pseudo.h` / `.pseudo.cpp` 伪代码骨架；不执行 `protoc` 代码生成，也不落真实 `CMakeLists.txt` / codegen 文件。

### 实现切片

| 步骤 | 内容 | 产出 |
|------|------|------|
| 1.1 | 固化 proto 分层与命名规则 | 确定 `common.proto`、`agent.proto`、`controller.proto`、`external.proto` 的 ownership、package、service 命名和 import 关系 |
| 1.2 | 先定义 `common.proto` | 固定共享 identity、metadata、ACK、sequence、load snapshot、错误枚举和路由公共结构 |
| 1.3 | 定义 `agent.proto` | 固定 `AgentService` 的首批方法、请求响应结构和错误码映射 |
| 1.4 | 定义 `controller.proto` | 固定 `ControllerService` 的注册、心跳、负载、退出与 reconcile 契约 |
| 1.5 | 定义 `external.proto` | 固定 `ExternalService` 的连接、重连、拉起、下行发包、移除会话契约，以及下行通知消息 |
| 1.6 | 预留 CMake / Codegen 接入点 | 本轮只在计划中保留后续接入位置，不创建真实 `CMakeLists.txt` 或 codegen 文件 |
| 1.7 | 固定未来 RPC 源码树 | 把 `rpc/<module>/<service>`、`handle_ss_rpc_*`、`handle_cs_rpc_*`、`task_action_*` 的目录层级写入计划并与 phase 切片对应 |
| 1.8 | 建立 DSA 服务入口骨架 | 伪代码中固定 `dsa_main`、`module_impl`、dispatcher 注册、`handle_ss_rpc_agentservice` 注册顺序 |
| 1.9 | 建立 DSC 服务入口骨架 | 伪代码中固定 `dsc_main`、`module_impl`、dispatcher 注册、`handle_ss_rpc_controllerservice` 与 `handle_cs_rpc_externalservice` 注册顺序 |
| 1.10 | 建立共享 runtime abstraction 骨架 | 配置加载、上下文对象、日志接口、定时器接口、request id / sequence 分配器、错误码桥接的伪代码 |

### 需要落的文件

- `src/shared/protocol/protocol/common/common.proto`
- `src/dsa/protocol/protocol/pbdesc/agent.proto`
- `src/dsc/protocol/protocol/pbdesc/controller.proto`
- `src/dsc/protocol/protocol/pbdesc/external.proto`
- `pseudocode/shared/runtime/runtime_abstractions.pseudo.h`
- `pseudocode/shared/runtime/runtime_abstractions.pseudo.cpp`
- `pseudocode/shared/runtime/runtime_abstractions_test.pseudo.cpp`
- `pseudocode/dsa/service/app/dsa_main.pseudo.h`
- `pseudocode/dsa/service/app/dsa_main.pseudo.cpp`
- `pseudocode/dsa/service/app/handle_ss_rpc_agentservice.pseudo.h`
- `pseudocode/dsa/service/app/handle_ss_rpc_agentservice.pseudo.cpp`
- `pseudocode/dsa/service/rpc/agentservice.pseudo.h`
- `pseudocode/dsa/service/rpc/agentservice.pseudo.cpp`
- `pseudocode/dsc/service/app/dsc_main.pseudo.h`
- `pseudocode/dsc/service/app/dsc_main.pseudo.cpp`
- `pseudocode/dsc/service/app/handle_ss_rpc_controllerservice.pseudo.h`
- `pseudocode/dsc/service/app/handle_ss_rpc_controllerservice.pseudo.cpp`
- `pseudocode/dsc/service/app/handle_cs_rpc_externalservice.pseudo.h`
- `pseudocode/dsc/service/app/handle_cs_rpc_externalservice.pseudo.cpp`
- `pseudocode/dsc/service/rpc/controllerservice.pseudo.h`
- `pseudocode/dsc/service/rpc/controllerservice.pseudo.cpp`
- `pseudocode/dsc/service/rpc/externalservice.pseudo.h`
- `pseudocode/dsc/service/rpc/externalservice.pseudo.cpp`

### 测试伪代码示例

```cpp
CASE_TEST(phase_1, proto_round_trip_start_ds_req_preserves_all_fields) {
	StartDSReq input;
	input.set_request_id(1001);
	input.set_expected_cpu(1.5f);
	input.set_expected_memory(2048.0f);
	input.add_custom_args("-log");

	byte_buffer encoded = serialize_proto(input);
	StartDSReq decoded = parse_proto<StartDSReq>(encoded);

	CASE_EXPECT_EQ(1001, decoded.request_id());
	CASE_EXPECT_TRUE(std::abs(decoded.expected_cpu() - 1.5f) < 0.0001f);
	CASE_EXPECT_TRUE(std::abs(decoded.expected_memory() - 2048.0f) < 0.0001f);
	CASE_EXPECT_EQ(1, decoded.custom_args_size());
}

CASE_TEST(phase_1, cmake_codegen_target_depends_on_proto_files) {
	build_graph graph = load_cmake_build_graph();
	target_t target = graph.find_target("atorbit_proto_codegen");

	CASE_EXPECT_TRUE(target.exists());
	CASE_EXPECT_TRUE(target.depends_on("src/dsc/protocol/protocol/pbdesc/controller.proto"));
	CASE_EXPECT_TRUE(target.output_contains("generated/controller.pb.cc"));
}

CASE_TEST(phase_1, rpc_scaffold_tree_contains_service_handle_and_task_action_layers) {
	file_tree tree = load_future_rpc_tree();

	CASE_EXPECT_TRUE(tree.exists("src/dsa/service/app/handle_ss_rpc_agentservice.cpp"));
	CASE_EXPECT_TRUE(tree.exists("src/dsa/service/rpc/agent/agentservice.cpp"));
	CASE_EXPECT_TRUE(tree.exists("src/dsc/service/app/handle_cs_rpc_externalservice.cpp"));
	CASE_EXPECT_TRUE(tree.exists("src/dsc/service/logic/action/task_action_launch_dedicated_server.cpp"));
}
```

## Phase 2: DSA 核心功能

### 目标

- 固化 DS 生命周期、资源账本、心跳和异常退出路径。
- 让 DSA 侧所有关键行为都能映射到明确函数和状态更新。
- 保持 DSA 侧实现与 `libatapp` 服务骨架、`libatbus` 转发抽象和 `atframe_utils` 工具风格一致。

### 实现切片

| 步骤 | 内容 | 产出 |
|------|------|------|
| 2.1 | 固化 DSA 服务入口与 RPC wiring | `dsa_main`、`module_impl`、dispatcher、`handle_ss_rpc_agentservice` 的装配顺序与依赖关系 |
| 2.2 | 完成 `AgentService::StartDS` 入站路径 | `agent.proto` → `rpc/agent/agentservice` → `handle_ss_rpc_agentservice` → `task_action_start_ds` → `start_ds_process` / `resource_ledger` / `local_channel_service` |
| 2.3 | 完成 `AgentService::StopDS` 入站路径 | `handle_ss_rpc_agentservice` → `task_action_stop_ds` → graceful stop / timeout escalate / kill path |
| 2.4 | 完成 `AgentService::QueryDSState` 与 drain 路径 | 查询单 DS 状态、查询 agent 运行快照、drain agent / reject new launch 的伪代码 |
| 2.5 | 完成 DS 本地注册与 ready 路径 | local channel register message → local dispatcher → DS registry / runtime state ready |
| 2.6 | 完成 DS 心跳路径 | local heartbeat message → `refresh_heartbeat_deadline` → heartbeat scanner → timeout transition |
| 2.7 | 完成 DS 退出归因路径 | waitpid / local exit notify / timeout / oom detect → unified `ds_exit_reason_t` → cleanup |
| 2.8 | 完成资源账本与容量路径 | reserve、commit、release、actual correction、oversubscribe detect、OOM candidate select |
| 2.9 | 完成负载聚合与周期上报路径 | per-DS sample → aggregate snapshot → invoke `rpc::controller::controllerservice::report_agent_load` |
| 2.10 | 完成注册、心跳、退出上报路径 | invoke `register_agent`、`heartbeat_agent`、`notify_ds_exit` facade，补齐 retry / backoff / failure logging |
| 2.11 | 完成 DSA 启动恢复与 reconcile 路径 | 启动扫描、重建运行中 DS 状态、向 DSC 上报当前 inventory 与 capacity snapshot |

### 需要落的文件

- `pseudocode/shared/runtime/resource_ledger.pseudo.h`
- `pseudocode/shared/runtime/resource_ledger.pseudo.cpp`
- `pseudocode/shared/runtime/resource_ledger_test.pseudo.cpp`
- `pseudocode/dsa/service/rpc/agentservice.pseudo.h`
- `pseudocode/dsa/service/rpc/agentservice.pseudo.cpp`
- `pseudocode/dsa/service/app/handle_ss_rpc_agentservice.pseudo.h`
- `pseudocode/dsa/service/app/handle_ss_rpc_agentservice.pseudo.cpp`
- `pseudocode/dsa/service/logic/action/task_action_start_ds.pseudo.h`
- `pseudocode/dsa/service/logic/action/task_action_start_ds.pseudo.cpp`
- `pseudocode/dsa/service/logic/action/task_action_stop_ds.pseudo.h`
- `pseudocode/dsa/service/logic/action/task_action_stop_ds.pseudo.cpp`
- `pseudocode/dsa/service/logic/action/task_action_query_ds_state.pseudo.h`
- `pseudocode/dsa/service/logic/action/task_action_query_ds_state.pseudo.cpp`
- `pseudocode/dsa/process/start_ds_process.pseudo.h`
- `pseudocode/dsa/process/start_ds_process.pseudo.cpp`
- `pseudocode/dsa/process/start_ds_process_test.pseudo.cpp`
- `pseudocode/dsa/agent/local_channel_service.pseudo.h`
- `pseudocode/dsa/agent/local_channel_service.pseudo.cpp`
- `pseudocode/dsa/heartbeat/heartbeat_monitor.pseudo.h`
- `pseudocode/dsa/heartbeat/heartbeat_monitor.pseudo.cpp`
- `pseudocode/dsa/heartbeat/heartbeat_monitor_test.pseudo.cpp`
- `pseudocode/dsa/agent/load_reporter.pseudo.h`
- `pseudocode/dsa/agent/load_reporter.pseudo.cpp`
- `pseudocode/dsa/agent/controller_reporter.pseudo.h`
- `pseudocode/dsa/agent/controller_reporter.pseudo.cpp`

### 测试伪代码示例

```cpp
CASE_TEST(phase_2, start_ds_reserves_capacity_and_launches_process) {
	ds_agent agent = build_agent_with_capacity(10.0f, 16384.0f);
	start_ds_request request = make_start_ds_request(2.0f, 1024.0f);
	mock_process_launcher launcher;
	agent.set_process_launcher(&launcher);
	launcher.set_start_process_result(process_handle_t::from_pid(4321));

	start_ds_response response = agent.start_ds(request);

	CASE_EXPECT_TRUE(response.ok());
	CASE_EXPECT_EQ(1, agent.get_running_ds_count());
	CASE_EXPECT_TRUE(std::abs(agent.get_available_cpu() - 8.0f) < 0.0001f);
	CASE_EXPECT_TRUE(std::abs(agent.get_available_memory() - 15360.0f) < 0.0001f);
}

CASE_TEST(phase_2, heartbeat_timeout_kills_process_and_reports_exit_reason) {
	ds_agent agent = build_agent_with_one_running_ds();
	agent.advance_time(kHeartbeatTimeoutMs + 1);

	agent.tick_heartbeat_scanner();

	CASE_EXPECT_TRUE(agent.was_process_killed(kTestDSID));
	CASE_EXPECT_EQ(ds_exit_reason_t::kHeartbeatTimeout, agent.get_exit_reason(kTestDSID));
	CASE_EXPECT_TRUE(agent.was_exit_reported_to_controller(kTestDSID));
}

CASE_TEST(phase_2, oom_protection_kills_largest_memory_process_first) {
	ds_agent agent = build_agent_with_three_running_ds();
	agent.set_pod_memory_usage_mb(16000.0f);
	agent.set_memory_kill_threshold_mb(15360.0f);

	agent.run_oom_protection();

	CASE_EXPECT_EQ(kLargestMemoryDSID, agent.get_killed_ds_id());
	CASE_EXPECT_EQ(ds_exit_reason_t::kOOMKill, agent.get_exit_reason(kLargestMemoryDSID));
}
```

## Phase 3: DSC 核心功能

### 目标

- 固化调度、会话路由、in-flight 管理和 DSA 断线清理。
- 让所有控制决策都对应到可验证的函数级伪代码。
- 所有调度、会话与断线逻辑都要能清晰落到 x-server 风格的组件目录和测试体系中。

### 实现切片

| 步骤 | 内容 | 产出 |
|------|------|------|
| 3.1 | 固化 DSC 服务入口与双向 RPC wiring | `dsc_main`、`module_impl`、`handle_ss_rpc_controllerservice`、`handle_cs_rpc_externalservice`、dispatcher 的装配顺序 |
| 3.2 | 完成 `ControllerService::RegisterAgent` 路径 | `controller.proto` → `rpc/controller/controllerservice` → `handle_ss_rpc_controllerservice` → `task_action_register_agent` → `agent_registry` / discovery bridge |
| 3.3 | 完成 `ControllerService::HeartbeatAgent` 路径 | `handle_ss_rpc_controllerservice` → `task_action_heartbeat_agent` → liveness refresh / reconnect state update |
| 3.4 | 完成 `ControllerService::ReportAgentLoad` 路径 | `task_action_report_agent_load` → scheduler cache / metrics / hot agent detection |
| 3.5 | 完成 `ControllerService::NotifyDSExit` 路径 | `task_action_notify_ds_exit` → DS ownership cleanup / inflight completion / owner notify |
| 3.6 | 完成 `ExternalService::ConnectExternal` 路径 | `external.proto` → `rpc/external/externalservice` → `handle_cs_rpc_externalservice` → `task_action_connect_external` → `session_router` |
| 3.7 | 完成 `ExternalService::ReconnectExternal` 路径 | `task_action_reconnect_external` → validate last received seq / rebuild route / mark replay window |
| 3.8 | 完成 `ExternalService::LaunchDedicatedServer` 路径 | `task_action_launch_dedicated_server` → `scheduler_service` → `reserve_inflight_slot` → `rpc::agent::agentservice::start_ds` |
| 3.9 | 完成调度评分路径 | region filter、metadata filter、capacity filter、sticky reuse、score、tie-break、fallback policy |
| 3.10 | 完成 inflight 生命周期路径 | reserve、timeout、completion、rollback、duplicate response guard |
| 3.11 | 完成会话与 DS 归属路径 | Unique ID session table、DS composite identity、owner binding、permission check、route inheritance |
| 3.12 | 完成 DSA 断线与 reconcile 清理路径 | node down event → `disconnect_cleanup` → remove agent / DS / inflight / session state，并向外部发通知 |

### 需要落的文件

- `pseudocode/dsc/service/rpc/controllerservice.pseudo.h`
- `pseudocode/dsc/service/rpc/controllerservice.pseudo.cpp`
- `pseudocode/dsc/service/rpc/externalservice.pseudo.h`
- `pseudocode/dsc/service/rpc/externalservice.pseudo.cpp`
- `pseudocode/dsc/service/app/handle_ss_rpc_controllerservice.pseudo.h`
- `pseudocode/dsc/service/app/handle_ss_rpc_controllerservice.pseudo.cpp`
- `pseudocode/dsc/service/app/handle_cs_rpc_externalservice.pseudo.h`
- `pseudocode/dsc/service/app/handle_cs_rpc_externalservice.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_register_agent.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_register_agent.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_heartbeat_agent.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_heartbeat_agent.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_report_agent_load.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_report_agent_load.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_notify_ds_exit.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_notify_ds_exit.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_connect_external.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_connect_external.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_reconnect_external.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_reconnect_external.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_launch_dedicated_server.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_launch_dedicated_server.pseudo.cpp`
- `pseudocode/dsc/registry/agent_registry.pseudo.h`
- `pseudocode/dsc/registry/agent_registry.pseudo.cpp`
- `pseudocode/dsc/registry/agent_registry_test.pseudo.cpp`
- `pseudocode/dsc/scheduler/scheduler_service.pseudo.h`
- `pseudocode/dsc/scheduler/scheduler_service.pseudo.cpp`
- `pseudocode/dsc/scheduler/scheduler_service_test.pseudo.cpp`
- `pseudocode/dsc/session/launch_flow.pseudo.h`
- `pseudocode/dsc/session/launch_flow.pseudo.cpp`
- `pseudocode/dsc/session/session_router.pseudo.h`
- `pseudocode/dsc/session/session_router.pseudo.cpp`
- `pseudocode/dsc/session/session_router_test.pseudo.cpp`
- `pseudocode/dsc/registry/disconnect_cleanup.pseudo.h`
- `pseudocode/dsc/registry/disconnect_cleanup.pseudo.cpp`

### 测试伪代码示例

```cpp
CASE_TEST(phase_3, select_agent_for_launch_picks_most_available_eligible_agent) {
	controller_service controller = build_controller_with_three_agents();

	agent_candidate selected = controller.select_agent_for_launch("region-cn-east", 1.0f, 1024.0f);

	CASE_EXPECT_TRUE(selected.found);
	CASE_EXPECT_EQ(kMostAvailableAgentID, selected.agent_id);
}

CASE_TEST(phase_3, launch_flow_reserves_inflight_before_sending_request) {
	controller_service controller = build_controller_with_one_eligible_agent();
	launch_request request = make_launch_request();

	launch_result result = controller.launch_dedicated_server(request);

	CASE_EXPECT_TRUE(result.pending());
	CASE_EXPECT_EQ(1, controller.get_inflight_count(kAgentID));
	CASE_EXPECT_TRUE(controller.was_start_ds_req_sent(kAgentID, result.request_id));
}

CASE_TEST(phase_3, dsa_reconnect_cleanup_removes_sessions_and_notifies_owners) {
	controller_service controller = build_controller_with_sessions_bound_to_agent(kAgentID);

	controller.handle_agent_disconnected(kAgentID);

	CASE_EXPECT_FALSE(controller.has_agent(kAgentID));
	CASE_EXPECT_FALSE(controller.has_ds_owned_by_agent(kAgentID));
	CASE_EXPECT_TRUE(controller.was_exit_notify_sent_to_owner(kOwnedDSAID, kOwnedDSID));
}
```

## Phase 4: 通信转发与 SDK

### 目标

- 固化外部服务、DSC、DSA、DS 之间的可靠消息转发。
- 把 Service 端 SDK 和 DS 端 SDK 的完整能力补齐到“可启动、可收发、可感知断链 / 退出、可主动停止自身”。
- 让 `LaunchDS -> DS ready -> 双向消息 -> DS self stop / DSM stop -> Service 侧通知` 成为可映射的函数级闭环。

### 实现切片

| 步骤 | 内容 | 产出 |
|------|------|------|
| 4.1 | 完成 `ExternalService::SendToDS` 路径 | `external.proto` → `rpc/external/externalservice` → `handle_cs_rpc_externalservice` → `task_action_send_to_ds` → `reliable_forwarder` |
| 4.2 | 完成下行 ACK 生命周期 | sequence allocate、pending queue、require-rsp tracking、ack success / failure completion |
| 4.3 | 完成上行入站路径 | DS / DSA 侧上行消息 ingress → dispatcher → upstream action → external route resolve |
| 4.4 | 完成重连重放路径 | `ReconnectExternal` → validate last_received_seq → replay buffered messages > last seq |
| 4.5 | 完成离线缓冲与容灾路径 | 按 owner uid 分区缓冲、TTL 清理、容量淘汰、dup guard |
| 4.6 | 完成唯一连接与路由固定路径 | 首连随机、重连 sticky、重复连接拒绝、owner mismatch reject |
| 4.7 | 完成 Service 端 SDK 契约路径 | `Connect`、`LaunchDS`、`SendToDS`、`OnDSMessage`、`OnDSDisconnected`、`OnDSExited`、ACK 语义 |
| 4.8 | 完成 DS 端 SDK 契约路径 | `Init`、`OnMessage`、`SendMessage`、`SendHeartbeat`、`StopSelf`、下行 ACK callback |
| 4.9 | 完成 DS ready / disconnect 事件路径 | `LaunchDSRsp` 只在 DS 完成 DSA 初始化后返回；DSA disconnect / DSM stop / route lost 统一映射为 Service 侧断链事件 |
| 4.10 | 完成异常重试与恢复策略 | resend、timeout fail、route switch、owner offline fallback、buffer overflow fail path |

### 需要落的文件

- `pseudocode/dsc/service/logic/action/task_action_send_to_ds.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_send_to_ds.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_handle_upstream_message.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_handle_upstream_message.pseudo.cpp`
- `pseudocode/dsc/service/logic/action/task_action_notify_ds_disconnect.pseudo.h`
- `pseudocode/dsc/service/logic/action/task_action_notify_ds_disconnect.pseudo.cpp`
- `pseudocode/dsc/forwarding/reliable_forwarder.pseudo.h`
- `pseudocode/dsc/forwarding/reliable_forwarder.pseudo.cpp`
- `pseudocode/dsc/forwarding/reliable_forwarder_test.pseudo.cpp`
- `pseudocode/dsc/forwarding/reconnect_replay.pseudo.h`
- `pseudocode/dsc/forwarding/reconnect_replay.pseudo.cpp`
- `pseudocode/dsc/forwarding/pending_ack_queue.pseudo.h`
- `pseudocode/dsc/forwarding/pending_ack_queue.pseudo.cpp`
- `pseudocode/dsc/forwarding/upstream_buffer_store.pseudo.h`
- `pseudocode/dsc/forwarding/upstream_buffer_store.pseudo.cpp`
- `pseudocode/sdk/external/external_client.pseudo.h`
- `pseudocode/sdk/external/external_client.pseudo.cpp`
- `pseudocode/sdk/external/external_event_stream.pseudo.h`
- `pseudocode/sdk/external/external_event_stream.pseudo.cpp`
- `pseudocode/sdk/external/external_client_test.pseudo.cpp`
- `pseudocode/sdk/ds/ds_client.pseudo.h`
- `pseudocode/sdk/ds/ds_client.pseudo.cpp`
- `pseudocode/sdk/ds/ds_runtime_loop.pseudo.h`
- `pseudocode/sdk/ds/ds_runtime_loop.pseudo.cpp`
- `pseudocode/sdk/ds/ds_client_test.pseudo.cpp`

### 测试伪代码示例

```cpp
CASE_TEST(phase_4, service_sdk_launch_returns_only_after_ds_ready) {
	external_client sdk = build_external_client();

	auto launch = sdk.launch_ds(make_launch_request());

	CASE_EXPECT_TRUE(launch.ok());
	CASE_EXPECT_TRUE(sdk.was_ds_ready_before_launch_returned(launch.ds_id));
}

CASE_TEST(phase_4, service_sdk_receives_disconnect_event_when_agent_disconnects) {
	controller_service controller = build_controller_with_online_session();

	controller.handle_agent_disconnected(kAgentID);

	CASE_EXPECT_TRUE(controller.was_disconnect_notify_sent_to_owner(kOwnedDSAID, kOwnedDSID));
}

CASE_TEST(phase_4, ds_sdk_stop_self_notifies_service_side_exit) {
	test_harness harness = build_harness_with_one_running_ds();

	harness.ds_sdk_stop_self(kOwnedDSID, 0, "normal_shutdown");

	CASE_EXPECT_TRUE(harness.external_received_exit_notify(kOwnedDSID));
}
```

## Phase 5: DSM 全局管理平面

### 目标

- 把 DSM 从占位目录提升为全局管理与运维控制平面。
- 让 DSM 不进入业务热路径，但能完整控制 DSC / DSA / DS 的 inventory、drain、stop 和 routing plan。
- 为平台 / 运维侧补齐统一的查询、审计和控制入口。

### 实现切片

| 步骤 | 内容 | 产出 |
|------|------|------|
| 5.1 | 固化 DSM 服务入口与 RPC wiring | `dsm_main`、`module_impl`、`handle_ss_rpc_managerservice`、dispatcher 的装配顺序 |
| 5.2 | 完成 `RegisterController` 路径 | `manager.proto` → `rpc/manager/managerservice` → `handle_ss_rpc_managerservice` → `task_action_register_controller` |
| 5.3 | 完成 `ReportControllerSnapshot` 路径 | DSC 周期上报 agent / ds / session / inflight 汇总，DSM 写入 `cluster_catalog` |
| 5.4 | 完成 `QueryInventory` 路径 | 按 Region、DSC、DSA、DS、owner uid 检索 inventory |
| 5.5 | 完成 `StopDedicatedServer` 路径 | DSM → DSC → DSA → DS 停服闭环，并回收 inventory |
| 5.6 | 完成 `DrainController` 路径 | 单个 DSC 进入 drain，拒绝新 LaunchDS / 新 session |
| 5.7 | 完成 `DrainRegion` 路径 | 批量对 Region 下所有 DSC 下发 drain 与维护窗口 |
| 5.8 | 完成 `ApplyRoutingPlan` 路径 | 黑名单 Controller、维护优先级、Region 路由策略切换 |
| 5.9 | 完成 DSM 审计与动作结果汇总 | action result、operator metadata、failure reason、retry / rollback 记录 |

### 需要落的文件

- `pseudocode/dsm/service/app/dsm_main.pseudo.h`
- `pseudocode/dsm/service/app/dsm_main.pseudo.cpp`
- `pseudocode/dsm/service/app/handle_ss_rpc_managerservice.pseudo.h`
- `pseudocode/dsm/service/app/handle_ss_rpc_managerservice.pseudo.cpp`
- `pseudocode/dsm/service/rpc/managerservice.pseudo.h`
- `pseudocode/dsm/service/rpc/managerservice.pseudo.cpp`
- `pseudocode/dsm/service/logic/action/task_action_register_controller.pseudo.h`
- `pseudocode/dsm/service/logic/action/task_action_register_controller.pseudo.cpp`
- `pseudocode/dsm/service/logic/action/task_action_report_controller_snapshot.pseudo.h`
- `pseudocode/dsm/service/logic/action/task_action_report_controller_snapshot.pseudo.cpp`
- `pseudocode/dsm/service/logic/action/task_action_query_inventory.pseudo.h`
- `pseudocode/dsm/service/logic/action/task_action_query_inventory.pseudo.cpp`
- `pseudocode/dsm/service/logic/action/task_action_stop_dedicated_server.pseudo.h`
- `pseudocode/dsm/service/logic/action/task_action_stop_dedicated_server.pseudo.cpp`
- `pseudocode/dsm/service/logic/action/task_action_drain_controller.pseudo.h`
- `pseudocode/dsm/service/logic/action/task_action_drain_controller.pseudo.cpp`
- `pseudocode/dsm/service/logic/action/task_action_drain_region.pseudo.h`
- `pseudocode/dsm/service/logic/action/task_action_drain_region.pseudo.cpp`
- `pseudocode/dsm/service/logic/action/task_action_apply_routing_plan.pseudo.h`
- `pseudocode/dsm/service/logic/action/task_action_apply_routing_plan.pseudo.cpp`
- `pseudocode/dsm/topology/cluster_catalog.pseudo.h`
- `pseudocode/dsm/topology/cluster_catalog.pseudo.cpp`
- `pseudocode/dsm/topology/cluster_catalog_test.pseudo.cpp`
- `pseudocode/dsm/control/maintenance_coordinator.pseudo.h`
- `pseudocode/dsm/control/maintenance_coordinator.pseudo.cpp`
- `pseudocode/dsm/control/routing_plan_service.pseudo.h`
- `pseudocode/dsm/control/routing_plan_service.pseudo.cpp`

### 测试伪代码示例

```cpp
CASE_TEST(phase_5, drain_region_marks_all_target_controllers_draining) {
	manager_service manager = build_manager_with_region("region-cn-east");

	auto result = manager.drain_region(make_drain_region_request("region-cn-east"));

	CASE_EXPECT_TRUE(result.ok());
	CASE_EXPECT_TRUE(manager.all_controllers_in_region_are_draining("region-cn-east"));
}

CASE_TEST(phase_5, stop_dedicated_server_propagates_to_service_side_notify) {
	test_harness harness = build_full_harness_with_manager();

	harness.manager_stop_ds(kOwnedDSAID, kOwnedDSID, "maintenance");

	CASE_EXPECT_TRUE(harness.external_received_disconnect_or_exit(kOwnedDSID));
}
```

## Phase 6: 可观测性与健壮性

### 目标

- 为关键路径补齐指标、日志、故障演练和守护逻辑。
- 让所有异常分支都能被观测、测试和回归验证。
- 将 DSM 也纳入统一的指标、日志和错误码映射体系。

### 实现切片

| 步骤 | 内容 | 产出 |
|------|------|------|
| 6.1 | 固化 shared metrics namespace 与 labels | phase 2~5 各 action、scheduler、forwarder、session_router、cluster_catalog 使用统一指标命名和标签字典 |
| 6.2 | DSA 指标接入 | DS 计数、资源使用、退出原因、心跳超时、OOM Kill、注册 / 心跳 / 负载上报结果 |
| 6.3 | DSC 指标接入 | agent 数量、调度结果、launch inflight、forwarding result、replay result、disconnect cleanup 统计 |
| 6.4 | DSM 指标接入 | controller inventory、drain action、stop ds action、routing plan apply、audit result |
| 6.5 | 日志规范 | 结构化日志字段、request id、controller id、agent id、DS composite id、owner uid、operator uid、error code |
| 6.6 | 错误码归类与观测桥接 | 内部返回码、协议 response code、metric label、告警维度之间的映射规则 |
| 6.7 | Dashboard / Alert 设计 | launch、disconnect、replay、OOM、drain backlog、routing drift 告警条件 |
| 6.8 | 故障回归矩阵 | 拉起失败、超时、缓冲溢出、断线恢复、DSM stop / drain、route mismatch、重复连接 |

### 需要落的文件

- `pseudocode/shared/metrics/metrics_registry.pseudo.h`
- `pseudocode/shared/metrics/metrics_registry.pseudo.cpp`
- `pseudocode/shared/metrics/metrics_label_dictionary.pseudo.h`
- `pseudocode/shared/metrics/metrics_label_dictionary.pseudo.cpp`
- `pseudocode/shared/runtime/structured_logging.pseudo.h`
- `pseudocode/shared/runtime/structured_logging.pseudo.cpp`
- `pseudocode/shared/runtime/error_code_mapping.pseudo.h`
- `pseudocode/shared/runtime/error_code_mapping.pseudo.cpp`
- `pseudocode/dsa/agent/failure_handler.pseudo.h`
- `pseudocode/dsa/agent/failure_handler.pseudo.cpp`
- `pseudocode/dsc/forwarding/failure_handler.pseudo.h`
- `pseudocode/dsc/forwarding/failure_handler.pseudo.cpp`
- `pseudocode/dsm/control/audit_log.pseudo.h`
- `pseudocode/dsm/control/audit_log.pseudo.cpp`

### 测试伪代码示例

```cpp
CASE_TEST(phase_6, heartbeat_timeout_emits_metric_and_structured_log) {
	ds_agent agent = build_agent_with_one_timed_out_ds();

	agent.tick_heartbeat_scanner();

	CASE_EXPECT_TRUE(agent.metric_value("dsa_heartbeat_timeout_total") > 0);
	CASE_EXPECT_TRUE(agent.log_contains("exit_reason", "heartbeat_timeout"));
}

CASE_TEST(phase_6, dsm_drain_action_emits_audit_and_result_metric) {
	manager_service manager = build_manager_with_region("region-cn-east");

	manager.drain_region(make_drain_region_request("region-cn-east"));

	CASE_EXPECT_TRUE(manager.metric_value("dsm_drain_region_total") > 0);
	CASE_EXPECT_TRUE(manager.audit_contains("action", "drain_region"));
}
```

## Phase 7: Mock、集成与回归

### 目标

- 构建可重复运行的 Mock 组件和端到端验证入口。
- 将前面阶段的函数级伪代码整合为系统级回归场景。
- 把 DSM / DSC / DSA / Service SDK / DS SDK 的最终闭环全部串起来。

### 实现切片

| 步骤 | 内容 | 产出 |
|------|------|------|
| 7.1 | DS Mock 进程契约 | 支持 register、heartbeat、exit、crash、假负载、下行 ACK、上行消息发送、StopSelf |
| 7.2 | 外部服务 Mock 契约 | 支持 connect、launch、send、receive upstream、disconnect / exit callback、reconnect |
| 7.3 | DSM Mock / admin harness | 支持 query inventory、stop ds、drain controller / region、routing plan 变更 |
| 7.4 | Mock agent / controller / manager RPC harness | 让 `AgentService`、`ControllerService`、`ExternalService`、`ManagerService` 的 facade 和 handle 可在多节点测试中独立驱动 |
| 7.5 | 端到端驱动 | 构建 CMake test target、fixture、进程拓扑与时钟推进接口，串联 DSM / DSC / DSA / Mock 组件 |
| 7.6 | 正常路径回归 | launch → DS ready → downstream send → upstream reply → DS self stop → owner notify |
| 7.7 | 控制平面回归 | DSM query inventory → DSM stop ds → DSM drain region → inventory convergence |
| 7.8 | 故障场景回归 | crash、死循环、OOM、agent disconnect、duplicate uid、buffer overflow、late ack、stale inflight response、routing drift |

### 需要落的文件

- `pseudocode/sdk/ds/mock_ds_process.pseudo.h`
- `pseudocode/sdk/ds/mock_ds_process.pseudo.cpp`
- `pseudocode/sdk/external/mock_external_service.pseudo.h`
- `pseudocode/sdk/external/mock_external_service.pseudo.cpp`
- `pseudocode/dsm/control/mock_manager_client.pseudo.h`
- `pseudocode/dsm/control/mock_manager_client.pseudo.cpp`
- `pseudocode/shared/runtime/mock_rpc_harness.pseudo.h`
- `pseudocode/shared/runtime/mock_rpc_harness.pseudo.cpp`
- `pseudocode/shared/runtime/multi_node_fixture.pseudo.h`
- `pseudocode/shared/runtime/multi_node_fixture.pseudo.cpp`
- `pseudocode/shared/runtime/failure_regression_matrix_test.pseudo.cpp`
- `pseudocode/shared/runtime/end_to_end_test_harness.pseudo.h`
- `pseudocode/shared/runtime/end_to_end_test_harness.pseudo.cpp`
- `pseudocode/shared/runtime/end_to_end_test_harness_test.pseudo.cpp`

### 测试映射示例

```cpp
CASE_TEST(phase_7, end_to_end_launch_message_stopself_disconnect_succeeds) {
	test_harness harness = build_full_harness_with_manager();
	harness.start_manager();
	harness.start_controller();
	harness.start_agent();
	harness.start_external_client();

	launch_result launch = harness.external_launch_ds();
	CASE_EXPECT_TRUE(launch.ok());

	send_result send = harness.external_send_to_ds(launch.dsa_id, launch.ds_id, "hello");
	CASE_EXPECT_TRUE(send.ok());
	CASE_EXPECT_TRUE(harness.external_received_upstream(launch.ds_id));

	harness.ds_sdk_stop_self(launch.ds_id, 0, "normal_shutdown");
	CASE_EXPECT_TRUE(harness.external_received_disconnect_or_exit(launch.ds_id));
}

CASE_TEST(phase_7, dsm_drain_region_blocks_new_launch_and_converges_inventory) {
	test_harness harness = build_full_harness_with_manager();
	harness.manager_drain_region("region-cn-east");

	CASE_EXPECT_FALSE(harness.external_launch_ds_in_region("region-cn-east").ok());
	CASE_EXPECT_TRUE(harness.manager_inventory_for_region("region-cn-east").draining());
}
```

## 与伪代码目录的对应关系

- Phase 1 优先落到 `pseudocode/proto/`、`pseudocode/shared/`，并搭起 `pseudocode/dsa/service/`、`pseudocode/dsc/service/` 下的 `app/`、`rpc/`、`logic/action/` 基础树。
- Phase 2 优先补全 `pseudocode/dsa/service/` 与 `pseudocode/dsa/process/`、`pseudocode/dsa/heartbeat/`、`pseudocode/dsa/agent/`。
- Phase 3 优先补全 `pseudocode/dsc/service/` 与 `pseudocode/dsc/registry/`、`pseudocode/dsc/scheduler/`、`pseudocode/dsc/session/`。
- Phase 4 继续扩展 `pseudocode/dsc/service/logic/action/`、`pseudocode/dsc/forwarding/` 与 `pseudocode/sdk/`，把 Service / DS 双侧 SDK 能力补齐。
- Phase 5 优先补全 `pseudocode/dsm/service/`、`pseudocode/dsm/topology/` 与 `pseudocode/dsm/control/`。
- Phase 6 优先补全 `pseudocode/shared/` 中的日志、指标和故障处理规范，并把 DSM 纳入统一观测面。
- Phase 7 补齐 Mock 与端到端回归伪代码，验证 DSM / DSC / DSA / SDK 全链路闭环。

## 正式代码落地约束

- 正式代码目录应优先落在与 atframework 组件一致的布局中，而不是延续伪代码目录名作为最终结构。
- 对外公共接口优先进入 `include/`，实现优先进入 `src/`，测试优先进入 `test/case/`。
- RPC 相关真实代码默认对齐 `src/dsa/service/app|logic/action|rpc/agent` 与 `src/dsc/service/app|logic/action|rpc/controller|rpc/external` 的分层。
- 若需要复用 atframework 现有模块生命周期、连接器、总线消息或测试框架，应在实现前先在伪代码中显式写出对应依赖点。