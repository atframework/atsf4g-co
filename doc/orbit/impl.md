# atorbit 第一期最小可运行闭环计划

> 版本：v0.2 Phase-1 | 日期：2026-04-29
>
> 目标：在不改变 `src/component/orbit` 现有目录结构框架的前提下，先打通一个最小可运行闭环：
> `Server connect -> Controller launch_client -> Agent start_client -> Client client_start -> Controller client_start_notify -> Server send_to_client / Client send_to_server -> Client client_exit`。

---

## 1. 第一期范围

第一期只保留最先该动、且能组成闭环的那一批步骤。

闭环定义：

1. Server 能连接 Controller。
2. Controller 能选择一个可用 Agent 并下发 `start_client`。
3. Agent 能启动本地 Client 并接收 `client_start`。
4. Agent 能把启动成功事件上报给 Controller。
5. Controller 能把启动成功事件通知给 Server。
6. Server 和 Client 之间能通过 Controller、Agent 完成最小双向消息转发。
7. Client 主动退出后，退出事件能逐层回到 Server。

本期先不追求：

- 完整 ACK / 重传实现
- reconnect replay 完整落地
- OOM 保护细化策略
- 心跳超时自动回收全链路
- 全量指标与完整测试矩阵
- DSM 相关能力

---

## 2. 最高优先级约束

### 2.1 不改变 orbit 目录结构框架

- 不新增新的顶层组件目录。
- Agent 仍保持在 `src/component/orbit/agent/`。
- Controller 仍保持在 `src/component/orbit/controller/`。
- Server 仍保持在 `src/component/orbit/sdk/server/`。
- Client 仍保持在 `src/component/orbit/sdk/client/`。

### 2.2 严禁修改代码生成文件

`src/component/.gitignore` 已明确覆盖下列 orbit 文件：

- `orbit/agent/app/handle_ss_rpc_*`
- `orbit/controller/app/handle_ss_rpc_*`
- `orbit/sdk/server/handle/handle_ss_rpc_*`
- `orbit/sdk/*/rpc/*`

这类文件属于代码生成产物，**第一期实现中不应该直接修改**。

实现原则：

- 可以使用这些文件。
- 可以依赖这些文件已经暴露出来的接口和注册关系。
- 不直接手改这些文件。
- 若必须变更它们，应该回到生成模板或生成输入处理，而不是在当前阶段直接改产物。

### 2.3 协议边界不回退

- 只使用当前正式 `.proto`：`common`、`client`、`server`、`agent`、`controller`。
- 不把旧版 `DSARegister`、`LaunchDSReq`、`ExternalForwardDown` 等旧协议重新写入实现。
- Region 继续作为部署与连接上下文，而不是业务消息字段。

---

## 3. 第一期实施顺序

严格按下面顺序推进：

1. Agent
2. Controller
3. Server
4. Client

原因：

- Agent 和 Controller 先闭环，Server / Client 接入才有稳定宿主。
- Server 侧依赖 Controller 的会话和通知路径。
- Client 侧 facade 最后收口，能避免过早绑定不稳定运行时语义。

---

## 4. Agent 第一期步骤

### A1. 建立 Agent 共享运行时上下文

代码落点：

- `src/component/orbit/agent/logic/action/` 下新增或补齐运行时辅助文件
- `src/component/orbit/agent/app/orbit_agent_main.cpp`

目标：

- 统一承载 `agent_id`
- 当前 Region
- Client 运行表
- 简化版 inflight 启动表
- 当前负载快照

本期要求：

- 先不做复杂资源账本，只保留最小状态容器。
- 所有 Agent `task_action_*` 都从同一上下文读写状态。

完成标准：

- `task_action_start_client`
- `task_action_client_start`
- `task_action_client_exit`
- `task_action_send_to_server`
- `task_action_forward_to_client`

都能访问同一份运行时状态。

### A2. 打通 `start_client -> client_start`

代码落点：

- `agent/logic/action/task_action_start_client.*`
- `agent/logic/action/task_action_client_start.*`

目标：

1. `CTAStartClientReq.args` 能落成本地启动参数。
2. Agent 能创建 pending Client 记录。
3. 收到 `STAClientStartReq` 后把 pending Client 切换为 running。
4. Agent 能向 Controller 发 `ATCNotifyClientStartedReq`。

本期要求：

- 先不做复杂的启动参数校验矩阵。
- 先不做完整容量调度，只保留启动所需最小预留记录。

完成标准：

- Controller 发 `start_client` 后，Agent 最终能把 `notify_client_started` 上报回去。

### A3. 打通 `send_to_server` 和 `forward_to_client`

代码落点：

- `agent/logic/action/task_action_send_to_server.*`
- `agent/logic/action/task_action_forward_to_client.*`

目标：

- 下行：`CTAForwardToClientReq -> ATSForwardToClientNotify`
- 上行：`STASendToServerNotify -> ATCForwardToServerReq`

本期要求：

- 先做无 ACK 的最小转发闭环。
- 只处理“目标 Client 在线”的主路径。

完成标准：

- Server 发给 Client 的消息可以到达 Client。
- Client 发给 Server 的消息可以回到 Server。

### A4. 打通 `client_exit`

代码落点：

- `agent/logic/action/task_action_client_exit.*`

目标：

1. 接收 `STAClientExitReq`。
2. 从运行时上下文中更新 Client 状态。
3. 向 Controller 发 `ATCNotifyClientExitReq`。

本期要求：

- 先只处理主动退出主路径。
- Crash、心跳超时、OOM 保护留到后续阶段。

---

## 5. Controller 第一期步骤

### C1. 建立 Controller 最小运行时表

代码落点：

- `src/component/orbit/controller/logic/action/` 下新增或补齐运行时辅助文件

目标：

- Agent 注册表
- Server 会话表
- Client 路由表

本期要求：

- 先不实现 replay 缓冲。
- 先不实现复杂 pending_ack 队列。

完成标准：

- `register_agent`、`connect`、`notify_client_started`、`notify_client_exit`、`send_to_client`、`forward_to_server` 能共享同一份状态。

### C2. 打通 Agent 注册与基础可用状态

代码落点：

- `controller/logic/action/task_action_register_agent.*`
- `controller/logic/action/task_action_heartbeat_agent.*`

目标：

1. 接收 `ATCRegisterAgentReq` 后建立 Agent 记录。
2. 接收 `ATCHeartbeatAgentReq` 后刷新在线状态。
3. 只保留“同 Region Agent 才接受”的硬约束。

本期要求：

- 先只实现单 Region 正常注册主路径。
- 先不做复杂断线恢复。

### C3. 打通 `connect -> launch_client`

代码落点：

- `controller/logic/action/task_action_connect.*`
- `controller/logic/action/task_action_launch_client.*`

目标：

1. 用 `unique_id` 建立 Server 会话。
2. 从当前可用 Agent 中选择一个目标 Agent。
3. 发出 `CTAStartClientReq`。

本期要求：

- 调度策略先做最小版本：选第一个可用 Agent。
- inflight 先保留简单计数，不做超时回收。
- `CTMLaunchClientRsp` 本期只保证 RPC 主路径返回，不承诺 ready 语义。

### C4. 打通启动成功、转发和退出通知

代码落点：

- `controller/logic/action/task_action_notify_client_started.*`
- `controller/logic/action/task_action_notify_client_exit.*`
- `controller/logic/action/task_action_send_to_client.*`
- `controller/logic/action/task_action_forward_to_server.*`

目标：

1. `ATCNotifyClientStartedReq -> CTMClientStartNotify`
2. `MTCSendToClientNotify -> CTAForwardToClientReq`
3. `ATCForwardToServerReq -> CTMForwardToServerNotify`
4. `ATCNotifyClientExitReq -> CTMClientEndNotify`

本期要求：

- 先用 `DClientIdentity` 建立最小路由绑定。
- 先只校验“该 Client 属于当前 unique_id”的主路径。
- 不做 replay。

---

## 6. Server 第一期步骤

### S1. 固化 Server 模块最小入口

代码落点：

- `src/component/orbit/sdk/server/module/orbit_server.*`

目标：

- 保存 `unique_id`
- 保存目标 Region / 已绑定 Controller
- 保存已拥有的 `DClientIdentity`
- 暴露最小四个动作：connect、launch_client、send_to_client、disconnect

本期要求：

- 先不做复杂连接恢复状态机。

### S2. 打通 Controller 入站通知

代码落点：

- `sdk/server/logic/action/task_action_client_start_notify.*`
- `sdk/server/logic/action/task_action_client_end_notify.*`
- `sdk/server/logic/action/task_action_forward_to_server.*`

目标：

1. 接收 `CTMClientStartNotify` 后建立 Client ready 路由。
2. 接收 `CTMForwardToServerNotify` 后把消息抛给业务回调。
3. 接收 `CTMClientEndNotify` 后把 Client 标记为 exited。

本期要求：

- 先不做 replay_messages 恢复处理。
- 先只实现主路径回调派发。

完成标准：

- Server 能看到 Client started、message、exited 三类事件。

---

## 7. Client 第一期步骤

### CL1. 在现有 `sdk/client/` 框架内补齐最小 facade

代码落点：

- `src/component/orbit/sdk/client/` 现有层级内新增 facade/运行时文件
- `src/component/orbit/sdk/client/rpc/` 的现有层级

注意：

- `sdk/client/` 当前只有 `rpc/agenttoclientservice/` 可直接依赖。
- 本期不能为了补 facade 去新造新的顶层 SDK 框架。

目标：

- `init`
- `send_message`
- `stop_self`
- 下行消息回调注册

### CL2. 打通 Client 主路径四个动作

目标映射：

1. `init -> STAClientStartReq`
2. `send_message -> STASendToServerNotify`
3. 下行接收 `ATSForwardToClientNotify`
4. `stop_self -> STAClientExitReq`

本期要求：

- 先不做完整 heartbeat loop。
- 先不做 ACK 元数据推进。
- 心跳只保留占位，不纳入第一期闭环验收。

完成标准：

- Client 能启动后注册自己。
- 能收下行消息。
- 能发上行消息。
- 能主动退出。

---

## 8. 第一期验收标准

通过以下最小场景即可认为第一期闭环成立：

1. 一个 Agent 成功向 Controller 注册。
2. 一个 Server 成功 connect 到 Controller。
3. Server 发起一次 `launch_client`。
4. Agent 完成本地启动并上报 `notify_client_started`。
5. Server 收到 `client_start_notify`。
6. Server 发一条消息，Client 能收到。
7. Client 回一条消息，Server 能收到。
8. Client 主动退出，Server 能收到 `client_end_notify`。

只要以上 8 步成立，就进入第二期；第二期再补：

- heartbeat 闭环
- ACK / 重传
- reconnect replay
- Agent 断线清理
- OOM / timeout 自动回收
- 完整日志、指标与测试矩阵
Agent 侧对应旧版 DSA，当前主落点在：

- `src/component/orbit/agent/app/`
- `src/component/orbit/agent/logic/action/`
- `src/component/orbit/sdk/agent/rpc/controllertoagentservice/`

### A1. 固化 Agent 服务入口与共享上下文

代码落点：

- `agent/app/handle_ss_rpc_clienttoagentservice.*`
- `agent/app/handle_ss_rpc_controllertoagentservice.*`
- `agent/app/orbit_agent_main.cpp`

伪代码参考：

- `doc/orbit/pseudocode/dsa/service/app/`
- `doc/orbit/pseudocode/dsa/agent/local_channel_service.pseudo.*`

实现步骤：

1. 确认两组入站 RPC handle 已完整注册到现有 `task_action_*`。
2. 在 Agent 现有子树内补齐共享运行时上下文，统一承载：
   - 本 Agent 的 `agent_id`
   - 所属 Region
   - 当前负载快照
   - Client 运行表
   - inflight 启动记录
3. 统一让 `logic/action` 下的 `task_action_*` 通过同一运行时上下文访问共享状态。

完成标准：

- Agent 入口只负责启动和 wiring，不在 handle 层直接写业务逻辑。
- 后续所有 Agent action 都能访问同一份运行时状态。

### A2. 完成 Client 启动与停止主流程

代码落点：

- `agent/logic/action/task_action_start_client.*`
- `agent/logic/action/task_action_stop_client.*`

伪代码参考：

- `doc/orbit/pseudocode/dsa/process/start_ds_process.pseudo.*`
- `doc/orbit/pseudocode/dsa/service/logic/action/`

实现步骤：

1. 将 `CTAStartClientReq.args` 映射到本地 Client 启动参数。
2. 合并预设参数与 `DClientStartArgs.custom_args[]`。
3. 在启动前执行容量检查，使用 `expected_cpu` 和 `expected_memory_mb` 做预留。
4. 建立 inflight 记录，并把本地进程句柄/状态挂到运行时上下文。
5. 将 `CTAStopClientReq` 的 `client_id` 和 `reason` 映射到本地停止路径。
6. 明确停止动作与正常退出、异常退出、超时退出的收敛逻辑。

完成标准：

- `start_client` 能创建可追踪的 pending/running Client 记录。
- `stop_client` 不直接跳过状态收敛，最终仍要汇总成统一退出事件。

### A3. 完成 Client 生命周期回调收敛

代码落点：

- `agent/logic/action/task_action_client_start.*`
- `agent/logic/action/task_action_client_exit.*`
- `sdk/agent/rpc/controllertoagentservice/`

伪代码参考：

- `doc/orbit/pseudocode/dsa/agent/controller_reporter.pseudo.*`

实现步骤：

1. 接收 `STAClientStartReq` 后生成完整 `DClientIdentity`。
2. 将 `client_addr`、`custom_data` 写入运行时上下文中的 Client 记录。
3. 通过 Agent 侧出站 RPC facade 向 Controller 发送 `ATCNotifyClientStartedReq`。
4. 接收 `STAClientExitReq` 后按 proto 枚举收敛退出原因。
5. 通过 Agent 侧出站 RPC facade 向 Controller 发送 `ATCNotifyClientExitReq`。

完成标准：

- Client 启动成功与退出事件都只通过正式 proto 对外上报。
- 不再保留旧版 DS/DSA 复合 ID 逻辑。

### A4. 完成心跳、资源账本与负载上报

代码落点：

- `agent/logic/action/task_action_client_heartbeat.*`
- Agent 现有子树内新增的运行时/账本辅助文件
- `sdk/agent/rpc/controllertoagentservice/`

伪代码参考：

- `doc/orbit/pseudocode/dsa/heartbeat/heartbeat_monitor.pseudo.*`
- `doc/orbit/pseudocode/dsa/agent/load_reporter.pseudo.*`
- `doc/orbit/pseudocode/shared/runtime/resource_ledger.pseudo.*`

实现步骤：

1. 用 `STAClientHeartbeatNotify.snapshot` 刷新单 Client 的 CPU/内存使用量。
2. 汇总生成 `DAgentLoadSnapshot`。
3. 将账本模型统一到 `expected` 与 `actual` 的最大值策略。
4. 实现心跳超时检测、OOM 保护阈值检测和对应退出路径。
5. 通过 `heartbeat_agent` 周期性向 Controller 上报聚合负载。

完成标准：

- Agent 负载对 Controller 可见。
- 心跳超时、OOM 保护和正常退出都走统一退出收口路径。

### A5. 完成 Agent 双向转发与 ACK 元数据挂接

代码落点：

- `agent/logic/action/task_action_forward_to_client.*`
- `agent/logic/action/task_action_send_to_server.*`
- `sdk/agent/rpc/controllertoagentservice/`

伪代码参考：

- `doc/orbit/pseudocode/dsa/agent/local_channel_service.pseudo.*`
- `doc/orbit/pseudocode/dsc/forwarding/reliable_forwarder.pseudo.*`

实现步骤：

1. 下行路径：`CTAForwardToClientReq -> ATSForwardToClientNotify`。
2. 上行路径：`STASendToServerNotify -> ATCForwardToServerReq`。
3. 在不修改 protobuf body 的前提下，为转发链路挂接 ACK/重传元数据。
4. 统一处理 Client 不在线、Agent 本地路由失效、重复消息等边界情况。

完成标准：

- Agent 能成为稳定的双向转发中继。
- ACK/重传信息只在传输元数据层实现，不污染正式 proto。

### A6. Agent 侧验收与日志/指标补齐

实现步骤：

1. 为启动、停止、心跳、退出、转发各主路径补齐日志点。
2. 补齐 Guide 中约定的 Agent 侧基础指标。
3. 准备最小回归验证：启动成功、启动失败、心跳超时、stop_client、双向转发。

---

## 3. Controller 实现步骤

Controller 侧对应旧版 DSC，当前主落点在：

- `src/component/orbit/controller/app/`
- `src/component/orbit/controller/logic/action/`
- `src/component/orbit/sdk/controller/rpc/agenttocontrollerservice/`
- `src/component/orbit/sdk/controller/rpc/servertocontrollerservice/`

### C1. 固化 Controller 服务入口与核心运行时表

代码落点：

- `controller/app/handle_ss_rpc_agenttocontrollerservice.*`
- `controller/app/handle_ss_rpc_servertocontrollerservice.*`
- `controller/app/orbit_controller_main.cpp`

伪代码参考：

- `doc/orbit/pseudocode/dsc/registry/agent_registry.pseudo.*`
- `doc/orbit/pseudocode/dsc/session/session_router.pseudo.*`
- `doc/orbit/pseudocode/shared/runtime/`

实现步骤：

1. 在 Controller 现有子树内建立统一运行时上下文。
2. 运行时上下文至少包含：
   - Agent 注册表
   - Agent 负载快照表
   - Server 会话表
   - Client 路由表
   - replay/pending 缓冲
3. 所有 `task_action_*` 统一从共享上下文获取状态，不在 action 之间用隐式静态状态传递。

完成标准：

- Controller 的 Agent 管理、Server 会话和 Client 路由不再分散在多个 action 私有状态里。

### C2. 完成 Agent 注册、心跳与断线清理

代码落点：

- `controller/logic/action/task_action_register_agent.*`
- `controller/logic/action/task_action_heartbeat_agent.*`

伪代码参考：

- `doc/orbit/pseudocode/dsc/registry/agent_registry.pseudo.*`
- `doc/orbit/pseudocode/dsc/registry/disconnect_cleanup.pseudo.*`

实现步骤：

1. 接收 `ATCRegisterAgentReq` 时建立 `agent_id` 主键记录。
2. 强制校验 Region 一致性，只接受同 Region Agent。
3. 接收 `ATCHeartbeatAgentReq` 时刷新 `DAgentLoadSnapshot` 和最后活跃时间。
4. 为 Agent 断线增加统一清理逻辑：
   - 删除 Agent 记录
   - 清理其下全部 Client 路由
   - 回收 inflight 启动计数

完成标准：

- Controller 能正确维护同 Region Agent 的在线状态和容量快照。

### C3. 完成启动调度与 inflight 控制

代码落点：

- `controller/logic/action/task_action_launch_client.*`
- `sdk/controller/rpc/agenttocontrollerservice/`

伪代码参考：

- `doc/orbit/pseudocode/dsc/scheduler/scheduler_service.pseudo.*`
- `doc/orbit/pseudocode/dsc/session/launch_flow.pseudo.*`

实现步骤：

1. 从 `MTCLaunchClientReq.args` 提取预期资源和启动参数。
2. 只在当前 Controller 所属 Region 的 Agent 集合中筛选目标 Agent。
3. 基于 `DAgentLoadSnapshot` 实现可用资源过滤。
4. 基于 `inflight_count` 实现并发限制。
5. 发送 `CTAStartClientReq` 前先预扣 inflight。
6. 在启动成功、启动失败、超时或 Agent 断线时回收 inflight。

完成标准：

- `launch_client` 只负责接受请求和发起调度，最终可用性以 `notify_client_started` 为准。

### C4. 完成 Server 会话、Client 路由与访问控制

代码落点：

- `controller/logic/action/task_action_connect.*`
- `controller/logic/action/task_action_disconnect.*`
- `controller/logic/action/task_action_notify_client_started.*`
- `controller/logic/action/task_action_notify_client_exit.*`
- `controller/logic/action/task_action_send_to_client.*`
- `controller/logic/action/task_action_forward_to_server.*`

伪代码参考：

- `doc/orbit/pseudocode/dsc/session/session_router.pseudo.*`

实现步骤：

1. 用 `unique_id` 建立 Server 会话主键。
2. 定义重复 `unique_id` 的处理策略，并在 `connect` 中实现。
3. 用 `DClientIdentity` 建立 Client 路由记录，并绑定所属 `unique_id`。
4. 在 `send_to_client` 中验证目标 `client_identity` 是否属于当前 `unique_id`。
5. 在 `notify_client_started` / `notify_client_exit` 中维护路由状态迁移。

完成标准：

- Controller 侧路由键统一为 `unique_id + DClientIdentity`，不回退到旧版 `(dsa_id, ds_id)` 方案。

### C5. 完成 ACK、离线缓冲与重连回放

代码落点：

- Controller 现有子树内新增的 forwarding/session 辅助文件
- `task_action_connect.*`
- `task_action_send_to_client.*`
- `task_action_forward_to_server.*`

伪代码参考：

- `doc/orbit/pseudocode/dsc/forwarding/pending_ack_queue.pseudo.*`
- `doc/orbit/pseudocode/dsc/forwarding/reconnect_replay.pseudo.*`
- `doc/orbit/pseudocode/dsc/forwarding/reliable_forwarder.pseudo.*`
- `doc/orbit/pseudocode/dsc/forwarding/upstream_buffer_store.pseudo.*`

实现步骤：

1. 为 Server <-> Controller 和 Controller <-> Agent 链路建立传输元数据结构。
2. 为上行消息建立 pending/replay 缓冲。
3. 在 `connect(reconnect=true)` 路径中把未确认的 `DClientMessage` 回填到 `CTMConnectRsp.replay_messages[]`。
4. 在 ACK 到达后推进 `ack_commit` 并清理已确认消息。
5. 为超时、重复 ACK、乱序 ACK、Agent 断线等情况补齐边界处理。

完成标准：

- 当前正式 proto 不变，但 Controller 已具备最小可用的重连回放能力。

### C6. Controller 侧验收与可观测性补齐

实现步骤：

1. 补齐 Agent 注册、调度选择、连接恢复、退出清理等关键日志。
2. 补齐 Guide 中约定的 Controller 指标。
3. 准备最小回归验证：
   - Agent 注册与心跳
   - Server connect/disconnect
   - launch_client 调度成功/失败
   - send_to_client / forward_to_server 双向转发
   - reconnect replay

---

## 4. Server 实现步骤

Server 侧对应旧伪代码 `sdk/external/`，当前主落点在：

- `src/component/orbit/sdk/server/module/`
- `src/component/orbit/sdk/server/handle/`
- `src/component/orbit/sdk/server/logic/action/`
- `src/component/orbit/sdk/server/rpc/controllertoserverservice/`

### S1. 固化 Server SDK 模块生命周期与公开入口

代码落点：

- `sdk/server/module/orbit_server.*`

伪代码参考：

- `doc/orbit/pseudocode/sdk/external/external_client.pseudo.*`

实现步骤：

1. 明确 Server SDK 的模块职责：
   - 保存 `unique_id`
   - 保存目标 Region/绑定 Controller
   - 维护拥有的 Client 路由
   - 驱动 connect / disconnect / launch_client / send_to_client
2. 将旧伪代码中的 `external_client` 语义映射为当前 `orbit_server_module` 对外接口。
3. 在模块层统一管理连接状态与回调注册入口。

完成标准：

- Server SDK 的业务入口集中在 `sdk/server/module/`，而不是散落到 RPC facade 或 handle 层。

### S2. 完成 Server 出站 RPC 封装

代码落点：

- `sdk/server/rpc/controllertoserverservice/`

伪代码参考：

- `doc/orbit/pseudocode/sdk/external/external_client.pseudo.*`

实现步骤：

1. 封装 `MTCConnectReq`、`MTCDisconnectReq`、`MTCLaunchClientReq`、`MTCSendToClientNotify` 的调用路径。
2. 保持 Region 只在 SDK 侧作为连接选择参数，不写入 on-wire proto。
3. 明确 `launch_client` 的语义：请求被接受不等于 Client ready，最终以 `CTMClientStartNotify` 为准。

完成标准：

- Server 业务层只看到稳定的 SDK 接口，不直接拼装 protobuf 请求。

### S3. 完成 Server 入站通知处理与路由状态维护

代码落点：

- `sdk/server/handle/handle_ss_rpc_controllertoserverservice.*`
- `sdk/server/logic/action/task_action_client_start_notify.*`
- `sdk/server/logic/action/task_action_client_end_notify.*`
- `sdk/server/logic/action/task_action_forward_to_server.*`

伪代码参考：

- `doc/orbit/pseudocode/sdk/external/external_event_stream.pseudo.*`

实现步骤：

1. 将 Controller 下发的三类入站消息注册到 Server SDK 的 action 层。
2. 在 `client_start_notify` 中把 `DClientIdentity` 绑定到已拥有路由。
3. 在 `client_end_notify` 中推进 Client 路由状态为 exited/disconnected。
4. 在 `forward_to_server` 中把 `DClientMessage` 分发给业务层回调。

完成标准：

- Server 侧所有上行业务消息与生命周期消息都通过正式 proto 事件回调进入业务层。

### S4. 完成 Server 重连回放与 ACK 接口对接

代码落点：

- `sdk/server/module/orbit_server.*`
- `sdk/server/logic/action/*`

伪代码参考：

- `doc/orbit/pseudocode/dsc/forwarding/reconnect_replay.pseudo.*`
- `doc/orbit/pseudocode/sdk/external/external_event_stream.pseudo.*`

实现步骤：

1. 在重连时处理 `CTMConnectRsp.replay_messages[]`。
2. 将 replay 消息按原顺序送入业务回调流。
3. 在 SDK 内部维护 ACK 进度，并向 Controller 侧回传传输元数据确认。
4. 定义业务层看到的路由状态：ready / disconnected / exited。

完成标准：

- Server 重连后可以自动恢复未确认上行消息流，不需要业务层手工重放。

---

## 5. Client 实现步骤

Client 侧对应旧伪代码 `sdk/ds/`，当前主落点在：

- `src/component/orbit/sdk/client/rpc/agenttoclientservice/`
- `src/component/orbit/sdk/client/` 现有根层级

说明：当前 `sdk/client/` 只有 `rpc/agenttoclientservice/`，因此 Client 侧实现必须保持在现有 `sdk/client/` 框架内推进，不新增新的顶层组件目录。

### CL1. 补齐 Client 侧运行时 facade

代码落点：

- `sdk/client/` 根目录下的新增 facade/运行时文件

伪代码参考：

- `doc/orbit/pseudocode/sdk/ds/ds_client.pseudo.*`
- `doc/orbit/pseudocode/sdk/ds/ds_runtime_loop.pseudo.*`

实现步骤：

1. 在 `sdk/client/` 现有层级内新增 Client facade，承载：
   - init
   - send_message
   - send_heartbeat
   - stop_self
   - 下行消息回调注册
2. 不额外新增新的 `module/logic/handle` 顶层分层，保持 Client SDK 轻量结构。

完成标准：

- Client 业务进程可以只依赖一个清晰的 facade，而不是直接依赖生成的 rpc 文件。

### CL2. 补齐 Client -> Agent 出站 RPC 封装

代码落点：

- `sdk/client/rpc/` 现有层级内补齐 ClientToAgentService 的 facade

伪代码参考：

- `doc/orbit/pseudocode/sdk/ds/ds_runtime_loop.pseudo.*`

实现步骤：

1. 在现有 `sdk/client/rpc/` 层内补齐对 `ClientToAgentService` 的请求封装。
2. 封装四类出站消息：
   - `STAClientHeartbeatNotify`
   - `STASendToServerNotify`
   - `STAClientStartReq`
   - `STAClientExitReq`
3. 统一复用 Agent 本地 channel 连接，不为每类消息单独建链。

完成标准：

- Client 侧四类出站 proto 都有稳定的 facade 可调用。

### CL3. 补齐 Agent -> Client 下行处理与回调派发

代码落点：

- `sdk/client/rpc/agenttoclientservice/`
- Client facade/运行时文件

伪代码参考：

- `doc/orbit/pseudocode/sdk/ds/ds_runtime_loop.pseudo.*`

实现步骤：

1. 消费 `ATSForwardToClientNotify` 并转成业务回调。
2. 消费 `ATSForkSeedClientNotify` 并驱动本地启动阶段初始化。
3. 将 ACK/seq 元数据挂接到运行时 loop，而不是写入 proto body。

完成标准：

- Client 能正确接收下行消息并进入统一的运行时循环。

### CL4. 完成 Client 启动、心跳、退出闭环

代码落点：

- Client facade/运行时文件
- `sdk/client/rpc/` 下相关 facade

伪代码参考：

- `doc/orbit/pseudocode/sdk/ds/ds_client.pseudo.*`

实现步骤：

1. `init` 成功后主动发送 `STAClientStartReq`。
2. 周期发送 `STAClientHeartbeatNotify`。
3. `send_message` 对应 `STASendToServerNotify`。
4. `stop_self` 对应 `STAClientExitReq`，并正确携带退出原因、退出码、附带数据。

完成标准：

- Client 侧形成完整的 Init -> Start -> Heartbeat -> Send -> Exit 闭环。

### CL5. Client 侧验收与集成验证

实现步骤：

1. 验证 `fork_seed_client` -> `client_start` 闭环。
2. 验证下行消息接收与上行消息发送。
3. 验证心跳超时、主动退出、异常退出三类退出场景。
4. 验证 ACK 元数据在 Client 链路上的推进不影响正式 proto body。

---

## 6. 建议实施节奏

建议严格按以下顺序推进：

1. 先完成 Agent A1-A4，保证本地生命周期、负载和上报闭环成立。
2. 再完成 Controller C1-C4，保证调度、会话、路由闭环成立。
3. 然后完成 Server S1-S4，让上游业务侧能稳定接入当前链路。
4. 最后完成 Client CL1-CL5，把本地运行时和出站 facade 收敛为正式 SDK。
5. ACK / replay 相关步骤优先在 Controller 和 Server 之间打通，再下沉到 Agent/Client 链路。

这样做的原因是：

- Agent 和 Controller 决定了服务端主链路能否闭环。
- Server 依赖 Controller 的会话与回放能力。
- Client SDK 依赖 Agent 本地协议和运行时语义已经稳定。