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
   agent 的可执行文件和启动参数为 `orbit_agent_cfg.client_path`、`client_command_line`，
   values 入口为 `orbit-agent.yaml`。
3. 业务 server 沿用 `src/orbitsvr/` 的 SDK 依赖与注册方式；
   UE DS 接入 `src/component/GameSharedComponent/Orbit/include/Orbit/` 的 client 运行时，
   参照 `OrbitClientRuntime.h`、`OrbitEasyApi.h` 和示例 `orbit_config.yaml`。
4. 先用已接入 runtime 的进程验证启动、心跳、消息回显和退出，再替换为实际 UE DS。
   agent 和被管理进程应使用匹配的身份及通信配置。
   仅修改进程路径、启动参数和已有模板数据时，使用配置入口即可。

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
