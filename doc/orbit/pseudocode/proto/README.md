# proto

本目录不定义新协议，只用于整理 `src/component/orbit` 下正式 `.proto` 的链路、字段和调用方向。

## 正式协议文件

| 文件 | 角色 | 当前结论 |
| --- | --- | --- |
| `src/component/orbit/protocol/common/protocol/common/orbit.common.proto` | 共享模型 | 定义 Client/Agent/Server 标识、负载快照、启动参数和退出原因 |
| `src/component/orbit/protocol/client/protocol/pbdesc/client_service.proto` | Client <-> Agent | 定义启动、心跳、退出和上下行消息 |
| `src/component/orbit/agent/protocol/protocol/pbdesc/agent_service.proto` | Agent -> Controller | 定义注册、负载上报、Client 生命周期和上行消息 |
| `src/component/orbit/controller/protocol/protocol/pbdesc/controller_service.proto` | Controller -> Agent | 定义启动、停止和下行转发 |
| `src/component/orbit/protocol/server/protocol/pbdesc/server_service.proto` | Server <-> Controller | 定义连接、重连、启动请求、下行发送和生命周期通知 |

## 共享字段

`orbit.common.proto` 当前已经固定了以下协议基础：

- `DClientIdentity = DAgentIdentity + DClientId`
- `DServerIdentity.unique_id`
- `DClientLoadSnapshot` 与 `DAgentLoadSnapshot`
- `DClientStartArgs` 与 `DAgentClientStartArgs`
- `DClientMessage`
- `EnClientState`
- `EnClientExitReason`

文档和伪代码应直接复用这些定义，不再额外发明平行结构。

## 三条正式链路

### 1. Client <-> Agent

来源：`client_service.proto`

- Agent 下行：`forward_to_client`、`fork_seed_client`
- Client 上行：`client_heartbeat`、`send_to_server`、`client_start`、`client_exit`

这条链路确认了 Client 的本地控制面和消息回传入口。

### 2. Agent <-> Controller

来源：`agent_service.proto` 与 `controller_service.proto`

- Agent 上行：`register_agent`、`heartbeat_agent`、`notify_client_started`、`notify_client_exit`、`forward_to_server`
- Controller 下行：`start_client`、`stop_client`、`forward_to_client`

这条链路确认了调度与路由的最小闭环，但没有定义 DSM 管理面。

### 3. Server <-> Controller

来源：`server_service.proto`

- Server 上行：`connect`、`disconnect`、`launch_client`、`send_to_client`
- Controller 下行：`forward_to_server`、`client_start_notify`、`client_end_notify`

`connect` 返回 `replay_messages`，说明重连回放已被预留，但 ACK、序列号和缓冲策略尚未形成正式协议字段。

## 端到端映射

- 启动链路：`launch_client` -> `start_client` -> `fork_seed_client` -> `client_start` -> `notify_client_started` -> `client_start_notify`
- 下行消息：`send_to_client` -> `forward_to_client` -> `forward_to_client`
- 上行消息：`send_to_server` -> `forward_to_server` -> `forward_to_server`
- 退出链路：`client_exit` -> `notify_client_exit` -> `client_end_notify`

## 暂不展开的内容

下列内容如果没有落进正式 `.proto`，本目录不继续总结：

- DSM RPC 和 inventory 查询
- controller / region 级运维动作
- ACK、序列号、超时重试和缓冲窗口细节
- 更细的调度算法和容灾策略