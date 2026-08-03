---
title: 服务总览
---

# 服务总览

## 业务服务（src/*svr/）

| 服务 | 说明 |
| --- | --- |
| `echosvr` | 最简示例服务：原样回显客户端数据（仅 `app/echosvr_main.cpp`），适合作为新服务模板 |
| `authsvr` | 认证/登录服：CS RPC `AuthsvrClientService`（`task_action_login_auth`），处理客户端登录鉴权 |
| `cachesvr` | 分布式对象缓存服：`cache_group` / `cache_group_manager` 管理缓存分组，对外提供 `cache_service.proto` 的 SS RPC，带 `sdk/` |
| `lobbysvr` | 大厅服（核心逻辑服）：登录、踢下线、玩家信息、玩家异步任务（`async_jobs/`）、全局/用户缓存（`cache/`）、排行榜查询（`rank/`）、dtmq 频道事件接收（`logic/dtmq/`），带 `sdk/` |
| `rank_settlement_svr` | 排行榜结算服：`rank_settlement_manager` + `task_action_rank_send_settlement / task_action_rank_update_settlement` |
| `orbitsvr` | Orbit 示例服务：演示 orbit 组件 server/client RPC |

## 组件服务（src/component/）

| 服务 | 组件 |
| --- | --- |
| `dtmq-proxysvr` | 分布式消息队列入口服务 |
| `dtcoordsvr` | 分布式事务协调者 |
| `rank_board_svr` | 排行榜榜单服务 |
| orbit controller / agent | Orbit 调度框架 |

## 内置接入服务（atframework/service/）

| 服务 | 说明 |
| --- | --- |
| `atgateway` | 客户端网关（ECDH 握手、加密、限流、路由切换） |
| `atproxy` | 跨服代理（etcd 服务发现、在线检测） |

## 服务目录约定

```
<name>svr/
├── service/ or app/        # main 入口（*_main.cpp）+ 生成的 RPC handler 注册
├── logic/                  # 业务逻辑与 task action
│   └── action/             # task_action_*（骨架由模板生成，业务填充）
├── sdk/                    # 可选：对外客户端 SDK
└── CMakeLists.txt          # 协议生成规则（服务名 + 模板 + 输出路径）
```

## 压测机器人

见[压测机器人（robot）](robot)。
