---
title: Orbit
---

# Orbit

Orbit 是针对 Unreal Engine（UE）的 Dedicated Server（DS）管理解决方案。
controller / agent / server / client 四层负责调度、节点进程管理、业务服接入与被管理进程通信。
Orbit 的 client 是被管理进程的角色，可用于 UE DS，不等同于玩家游戏客户端。

位置：`src/component/orbit/`。

## 快速上手

1. 在 values 中启用 `orbit-controller`、`orbit-agent` 和 `orbit-server`；
   `orbit-server` chart 对应 `src/orbitsvr/` 示例。
2. 配置 `resource/ExcelTables/OrbitClient.xlsx` 的进程模板并导出。
   在 values 的 `orbit-agent.yaml` 中设置 `orbit_agent.client_path` 和
   `orbit_agent.client_command_line`；生成配置的类型为 `orbit_agent_cfg`。
3. 业务 server 沿用 `src/orbitsvr/` 的 SDK 依赖与注册方式；
   UE DS 接入 `src/component/GameSharedComponent/Orbit/include/Orbit/` 的 client 运行时，
   参照 `OrbitClientRuntime.h`、`OrbitEasyApi.h` 和示例 `orbit_config.yaml`。
4. 先用已接入 runtime 的进程验证启动、心跳、消息回显和退出，再替换为实际 UE DS。
   agent 和被管理进程应使用匹配的身份及通信配置。
   仅修改进程路径、启动参数和已有模板数据时，使用配置入口即可。

`OrbitAutoClient` 是位于 `src/component/GameSharedComponent/Orbit/AutoClient/` 的 C++14 示例客户端，
日志路径使用 `atfw::util::file_system`，不需要 `std::filesystem`。

## 房间与用户流程

1. Match 通过 `orbit_room_create` 创建初始房间；房间启动后通过 `room_ready` 回传连接信息、玩家 token 和
   绝对 `end_join_timepoint`。等待室后补使用 `orbit_user_join` 向已有房间初始化用户，并由
   `is_last_one` 标记是否最后一批。
2. DS 与 Orbit server 之间通过 `user_init`、`remote_start_client`、`room_status`、`client_event`、心跳、
   `end_join` 和 `user_quit` 等入口推进状态。所有异步回调都需要校验房间、用户和当前生命周期。
3. 房间通过 DTMQ `EN_ORBIT_CHANNEL_TYPE_ROOM=101` 发布 `DOrbitRoomEventLog`。Lobby 的
   `user_get_info(need_user_orbit_room)` 返回 room key、地址、token 和 init 等信息。
4. Orbit 聊天类型 102 目前只有配置和协议预留，不等于房间聊天链路已完成；详见[聊天](chat)。

Match 的 EndJoin/UserQuit action 当前仍是返回成功的占位实现，Lobby 的结算结果处理也仍有 TODO。
因此房间创建、用户初始化和退出通知不能被描述为已经完成匹配侧结算闭环。

## 定制与详细设计

### 组成

| 部分 | 说明 |
| --- | --- |
| `controller/` | 控制器服务：全局调度决策 |
| `agent/` | 代理服务：节点级代理 |
| `protocol/` | client / common / server 三组 proto |
| `sdk/` | agent / client / controller / server 四套 SDK；server SDK 自带专用 Mako 模板 |

### 代码生成

orbit server SDK 使用自己的模板（`orbit/sdk/server/template/`）：
`handle_orbit_rpc` / `task_action_orbit_rpc` / `rpc_call_api_for_orbit`，生成 `handle_orbit_rpc_*`、
`task_action_orbit_rpc` 等代码。

### 示例服务

`src/orbitsvr/` 演示了 orbit 组件的用法：`handle_orbit_rpc_orbitserverrpcservice`（orbit server RPC
handler）与 `task_action_echo`。

### 客户端运行时

`src/component/GameSharedComponent/Orbit/` 提供客户端侧（如 UE）的 Orbit 运行时 SDK；`resource/UeSource*`
存放 UE 协议源数据。
