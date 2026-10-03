---
title: Services Overview
---

# Services Overview

## Quick Start

[Generate local configuration](../getting-started/run-deploy), then enable the services you need in
`proc_desc`. Consumers access other services through SDKs; see [components](../components/overview).

| Service/deployment chart | Purpose | Minimal validation entry |
| --- | --- | --- |
| `echosvr` | Raw message echo example | Send data through atgateway and check the echo |
| `authsvr` | Login authentication, `AuthsvrClientService` | robot login request and authentication response |
| `cachesvr` | Distributed object cache with SDK | Follow existing calls in the lobby's `cache/` |
| `lobbysvr` | Player login/data, friends, teams, matching, notifications | Login with robot and run existing business commands |
| `friendsvr-management` / `friendsvr-recommend` | Friend management; recommendation strategies remain unimplemented | [Friend quick start](../components/friend) |
| `matchsvr` | Matchmaking pools and rooms | [Matchmaking quick start](../components/matching-team) |
| `teamsvr-room` / `teamsvr-match` | Team rooms and team matching | [Team quick start](../components/matching-team) |
| `dtmq-proxysvr` | Message channels, subscriptions, synchronization | [DTMQ quick start](../components/dtmq) |
| `dtcoordsvr` | Distributed transaction coordinator | [Transaction quick start](../components/distributed-transaction) |
| `rank-board-svr` / `rank-settlement-svr` | Leaderboards and periodic settlement | [Rank quick start](../components/rank) |
| `orbit-controller` / `orbit-agent` / `orbit-server` | UE DS management; `orbitsvr` implements the server example | [Orbit quick start](../components/orbit) |
| `atgateway` / `atproxy` | Client access and cross-server traffic | [Access configuration](../architecture/gateway-proxy) |

Chart names containing hyphens can differ from source directory names containing underscores.
Use the actual chart name when declaring instances.

### Authentication and Lobby

Enable `authsvr`, `lobbysvr`, and the required gateway, proxy, Redis/component instances through the existing
configuration workflow. Log in with robot, then run an existing user-info or business command and inspect responses
and client notifications. Start new login policies or lobby methods with the [RPC quick start](../development/add-rpc-task).

### Object Cache

Enable `cachesvr` and configure Redis. Add `cachesvr-sdk` to the consumer's existing `USE_SERVICE_SDK`
and follow `src/lobbysvr/service/logic/cache/user_cache_manager.h/.cpp` for cache APIs.
Verify reads/updates for the same object, then integrate update notifications if subscriptions are needed.

### Echo Example

Point the test gateway at `echosvr`, send test data, and verify the unchanged echo.
Use it to test the access path; use a standard service template for user/session validation and business RPCs.

## Add a Service

See [adding a service](../development/add-service). Keep standard shared setup and handler registration,
and implement business actions. To extend an existing service, follow the [RPC quick start](../development/add-rpc-task).

## Customization and Directory Conventions

```text
<name>svr/
├── protocol/       # Optional: service protocols and config
├── service/        # app/ entry points, logic/ business code and task actions
├── sdk/            # Optional: public APIs
└── CMakeLists.txt  # Declarations using existing helpers
```

A service can contain several process roles; use the source layout as authority.
For setup changes, consult `src/server_frame/logic/logic_server_setup.h`.
See [robot](robot) for stress testing and simulated clients.
