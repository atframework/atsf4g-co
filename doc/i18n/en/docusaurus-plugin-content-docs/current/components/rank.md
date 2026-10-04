---
title: Rank Board (rank)
---

# Rank Board (rank)

Sharded rank board component: board data is stored sharded across the `rank_board_svr` cluster, with support
for mirrors, WAL master-standby sync, and periodic settlement.

Location: `src/component/rank/`.

## Quick Start

1. Enable `rank-board-svr`, adding `rank-settlement-svr` for periodic settlement, and prepare Redis.
2. Edit rules, board definitions, and reward pools in `resource/ExcelTables/Rank.xlsx`; export and publish them.
3. Add `rank-board-svr-sdk` and `rank-logic-sdk` to `USE_COMPONENTS` as needed.
   Public APIs live in `src/component/rank/sdk/rank_board_svr/rpc/rank_board/rank.h`.
4. Follow the lobby's `rank/` integration, write test scores, and query the board/ranks.
   For periodic settlement, verify results and reward jobs. Actual delivery requires a registered business handler;
   ordinary board configuration needs no template changes.

## Customization and Design

### Composition

| Part | Location | Description |
| --- | --- | --- |
| `rank_board_svr` | `rank/rank_board_svr/` | Board service: `rank` / `rank_manager` / `rank_mirror_*` mirrors / `rank_wal_handle` |
| SDK | `rank/sdk/` | `rank_board` (sharded RPC client) + `rank_logic` (ranking algorithms) |
| Protocol | `rank/protocol/` | `rank_board_service.proto` |

### Service Capabilities (task action)

Set/modify score, get top, heartbeat, master-standby switchover, etc. WAL master-standby sync uses the same
`distributed_system::wal_publisher / wal_subscriber` mechanism as dtmq (`rank_wal_handle`).
Currently `rank_wal_handle`'s `send_snapshot` callback only logs and does not send a complete snapshot.
Recovery beyond retained increments cannot rely on that callback alone; inspect the primary/standby data-pull path.

### Settlement

`src/rank_settlement_svr/` (`rank_settlement_manager` +
`task_action_rank_send_settlement / task_action_rank_update_settlement`) handles periodic board settlement: pulling boards
from rank_board_svr, creating reward jobs, adjusting scores, and saving history.
It stores reward jobs through `rpc::async_jobs::add_jobs`. The lobby currently has no registered `settle_rank`
business callback; item or mail delivery still needs implementation.

### Business Integration

The `rank/` directory in lobbysvr demonstrates query integration. To integrate: link the rank SDK and use
the `rank_board` client with shard addressing to send the RPCs in `rank_board_service.proto`.
