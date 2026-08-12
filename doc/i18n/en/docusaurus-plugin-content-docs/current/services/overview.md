---
title: Services Overview
---

# Services Overview

## Business Services (src/*svr/)

| Service | Description |
| --- | --- |
| `echosvr` | Minimal example service: echoes client data as-is (only `app/echosvr_main.cpp`), suitable as a template for new services |
| `authsvr` | Authentication/login service: CS RPC `AuthsvrClientService` (`task_action_login_auth`), handles client login authentication |
| `cachesvr` | Distributed object cache service: `cache_group` / `cache_group_manager` manage cache groups, exposes the SS RPC of `cache_service.proto`, ships with `sdk/` |
| `lobbysvr` | Lobby service (core logic service): login, kick-offline, user info, user async jobs (`async_jobs/`), global/user cache (`cache/`), rank board queries (`rank/`), dtmq channel event receiving (`logic/dtmq/`), ships with `sdk/` |
| `rank_settlement_svr` | Rank board settlement service: `rank_settlement_manager` + `task_action_rank_send_settlement / task_action_rank_update_settlement` |
| `orbitsvr` | Orbit example service: demonstrates the orbit component's server/client RPC |

## Component Services (src/component/)

| Service | Component |
| --- | --- |
| `dtmq-proxysvr` | Distributed message queue entry service |
| `dtcoordsvr` | Distributed transaction coordinator |
| `rank_board_svr` | Rank board service |
| orbit controller / agent | Orbit scheduling framework |

## Built-in Access Services (atframework/service/)

| Service | Description |
| --- | --- |
| `atgateway` | Client gateway (ECDH handshake, encryption, rate limiting, routing handoff) |
| `atproxy` | Cross-service proxy (etcd service discovery, online detection) |

## Service Directory Conventions

```
<name>svr/
├── service/ or app/        # main entry (*_main.cpp) + generated RPC handler registration
├── logic/                  # Business logic and task actions
│   └── action/             # task_action_* (skeletons generated from templates, filled with business logic)
├── sdk/                    # Optional: outbound client SDK
└── CMakeLists.txt          # Protocol generation rules (service name + templates + output paths)
```

## Stress-Test Robot

See [Stress-Test Robot (robot)](robot).
