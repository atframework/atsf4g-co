---
title: 排行榜（rank）
---

# 排行榜（rank）

分片排行榜组件：榜单数据分片存储在 `rank_board_svr` 集群，支持镜像、WAL 主备同步与周期结算。

位置：`src/component/rank/`。

## 快速上手

1. 启用 `rank-board-svr`；需要周期结算时增加 `rank-settlement-svr`，准备 Redis。
2. 修改 `resource/ExcelTables/Rank.xlsx` 中的规则、榜单定义和奖励池，导出并发布资源。
3. 消费方 `USE_COMPONENTS` 按需加入 `rank-board-svr-sdk` 与 `rank-logic-sdk`；
   公开接口在 `src/component/rank/sdk/rank_board_svr/rpc/rank_board/rank.h`。
4. 参照大厅服的 `rank/` 查询入口，写入测试分数并查询榜单/排名；
   使用周期结算时验证结算结果与奖励发放。普通榜单配置不需要修改生成模板。

## 定制与详细设计

### 组成

| 部分 | 位置 | 说明 |
| --- | --- | --- |
| `rank_board_svr` | `rank/rank_board_svr/` | 榜单服务：`rank` / `rank_manager` / `rank_mirror_*` 镜像 / `rank_wal_handle` |
| SDK | `rank/sdk/` | `rank_board`（分片 RPC 客户端）+ `rank_logic`（排名算法） |
| 协议 | `rank/protocol/` | `rank_board_service.proto` |

### 服务能力（task action）

set/modify score、get top、心跳、主备切换等；WAL 主备同步使用与 dtmq 相同的
`distributed_system::wal_publisher / wal_subscriber` 机制（`rank_wal_handle`）。

### 结算

`src/rank_settlement_svr/`（`rank_settlement_manager` +
`task_action_rank_send_settlement / task_action_rank_update_settlement`）负责榜单周期结算：从 rank_board_svr 拉取榜单，
发奖并写入结算结果。

### 业务接入

lobbysvr 的 `rank/` 目录演示了查询接入；接入方式：链接 rank SDK，用 `rank_board` 客户端按分片寻址发送
`rank_board_service.proto` 中的 RPC。
