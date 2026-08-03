---
title: Orbit
---

# Orbit

Orbit 是一套调度框架（controller / agent / server / client 四层），用于管理动态创建的逻辑实例。

位置：`src/component/orbit/`。

## 组成

| 部分 | 说明 |
| --- | --- |
| `controller/` | 控制器服务：全局调度决策 |
| `agent/` | 代理服务：节点级代理 |
| `protocol/` | client / common / server 三组 proto |
| `sdk/` | agent / client / controller / server 四套 SDK；server SDK 自带专用 Mako 模板 |

## 代码生成

orbit server SDK 使用自己的模板（`orbit/sdk/server/template/`）：
`handle_orbit_rpc` / `task_action_orbit_rpc` / `rpc_call_api_for_orbit`，生成 `handle_orbit_rpc_*`、
`task_action_orbit_rpc` 等代码。

## 示例服务

`src/orbitsvr/` 演示了 orbit 组件的用法：`handle_orbit_rpc_orbitserverrpcservice`（orbit server RPC
handler）与 `task_action_echo`。

## 客户端运行时

`src/component/GameSharedComponent/Orbit/` 提供客户端侧（如 UE）的 Orbit 运行时 SDK；`resource/UeSource*`
存放 UE 协议源数据。
