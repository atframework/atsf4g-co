---
title: Components Overview
---

# Components and Reusable Services

The project provides standalone components and reusable business services.
Prepare the services/configuration, declare SDK dependencies in the consumer, then use public APIs.
See [component integration](../development/add-component).

## Choose by Feature

| Feature | Services/location | Consumer SDK declaration |
| --- | --- | --- |
| [Message queues](dtmq) | `src/component/dtmq/`, `dtmq-proxysvr` | `USE_COMPONENTS "dtmq-proxy-sdk"` |
| [Distributed transactions](distributed-transaction) | `src/component/distributed_transaction/`, `dtcoordsvr` | `USE_COMPONENTS "distributed-transaction-sdk"` |
| [Leaderboards](rank) | `src/component/rank/`, `rank_board_svr` | `USE_COMPONENTS "rank-board-svr-sdk" "rank-logic-sdk"` |
| [Friends](friend) | `src/friendsvr/`, management and recommendation services | `USE_SERVICE_SDK "friend-sdk-management"` |
| [Matchmaking and teams](matching-team) | `src/matchsvr/`, `src/teamsvr/` | `USE_SERVICE_SDK "matchsvr-sdk" "team-common-sdk" "team-sdk-room"` |
| [UE DS management (Orbit)](orbit) | `src/component/orbit/`, controller / agent / server / client | Select SDKs by process role; see the Orbit quick start |
| Shared algorithms | `src/component/GameSharedComponent/` | `USE_COMPONENTS "ItemAlgorithmSDK" "BattleUtilitySDK"` |

Append these parameters to existing service declarations and select dependencies for the APIs you use;
do not redefine service targets. Algorithm SDKs need no separate process.
Orbit's client runtime also lives under `GameSharedComponent/Orbit/`.

## Quick Start

1. For service components, follow [run and deploy](../getting-started/run-deploy) and prepare Redis, Excel,
   and instances as needed. For algorithm SDKs, start with the dependency declaration.
2. Follow `src/lobbysvr/service/CMakeLists.txt` to add SDK parameters and reconfigure/build.
3. Call the SDK from business actions; register notification handlers for components that require them.
4. Verify requests/responses and required notifications for services, or call results for algorithms. `lobbysvr` demonstrates DTMQ, rank,
   friends, matchmaking, and teams. `orbitsvr` demonstrates Orbit server integration.

## Customization and Design

Service components commonly contain `protocol/`, service implementations, and `sdk/`;
pure algorithm components provide libraries only.
See [adding components](../development/add-component) for protocol, generation, and linking declarations.
Each component's design sections cover persistence, replication, recovery, and lifecycle rules.
