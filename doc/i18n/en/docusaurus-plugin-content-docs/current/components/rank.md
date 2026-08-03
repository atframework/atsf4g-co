---
title: Rank Board (rank)
---

# Rank Board (rank)

Sharded rank board component: board data is stored sharded across the `rank_board_svr` cluster, with support
for mirrors, WAL master-standby sync, and periodic settlement.

Location: `src/component/rank/`.

## Composition

| Part | Location | Description |
| --- | --- | --- |
| `rank_board_svr` | `rank/rank_board_svr/` | Board service: `rank` / `rank_manager` / `rank_mirror_*` mirrors / `rank_wal_handle` |
| SDK | `rank/sdk/` | `rank_board` (sharded RPC client) + `rank_logic` (ranking algorithms) |
| Protocol | `rank/protocol/` | `rank_board_service.proto` |

## Service Capabilities (task action)

Set/modify score, get top, heartbeat, master-standby switchover, etc. WAL master-standby sync uses the same
`distributed_system::wal_publisher / wal_subscriber` mechanism as dtmq (`rank_wal_handle`).

## Settlement

`src/rank_settlement_svr/` (`rank_settlement_manager` +
`task_action_rank_send_settlement / task_action_rank_update_settlement`) handles periodic board settlement: pulling boards
from rank_board_svr, granting rewards, and writing settlement results.

## Business Integration

The `rank/` directory in lobbysvr demonstrates query integration. To integrate: link the rank SDK and use
the `rank_board` client with shard addressing to send the RPCs in `rank_board_service.proto`.
