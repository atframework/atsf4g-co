---
title: 匹配与组队
---

# 匹配与组队

`src/matchsvr/` 提供匹配池、匹配房间与匹配结果通知；
`src/teamsvr/` 提供组队房间；其 `teamsvr-match` 队友搜索服务仍是占位实现。
大厅服的 `logic/matching/`、`logic/team/` 提供玩家侧接入。

## 快速上手：匹配

1. 启用 `matchsvr` 实例并准备其 Redis 配置。修改
   `resource/ExcelTables/Matching.xlsx` 中的匹配池、规则组、撮合规则与参数合并规则；
   关卡配置参照 `Level.xlsx`。
2. 按[Excel 上手](../development/excel-config)构建与发布配置；
   消费方 `USE_SERVICE_SDK` 加入 `matchsvr-sdk`。
3. 使用 `src/matchsvr/sdk/rpc/matching/matchsvrservice.atfw.gen.h` 的生成接口，
   玩家侧流程参照大厅服的 `user_matching_manager.h/.cpp`。
   结果通知沿用 `MatchsvrNotifyService` 的 handler 声明与注册。
4. 使用 `src/robot/case_config/matching_demo.conf` 的已有场景，验证发起、确认、取消及结果通知。
   组队匹配场景可参照 `matching_team_3v3.conf`。

只调整已有匹配参数时修改 Excel 和 values 即可。新增算法或撮合语义需要实现业务逻辑。
匹配结果后的战斗进程管理见[Orbit](orbit)。

## 快速上手：组队

1. 启用 `teamsvr-room`；队伍进入战斗匹配时配套 `matchsvr`。
   修改 `resource/ExcelTables/Team.xlsx` 的组队类型配置并导出。
2. 消费方 `USE_SERVICE_SDK` 加入 `team-common-sdk`、`team-sdk-room`。
   房间接口位于 `src/teamsvr/sdk/room/rpc/team/team_room_client_api.h`。
3. 参照 `src/lobbysvr/service/logic/team/user_team_manager.h/.cpp` 接入成员、邀请、队长和状态同步。
4. 用测试用户验证创建队伍、邀请/加入、离开，以及所需的匹配流程；检查客户端状态与通知。

`teamsvr-match` 的 `TeamMatchService.search` 尚未实现寻找队友逻辑；它与队伍进入 `matchsvr` 撮合的流程不同，
不属于上述最小接入。

## 定制与详细设计

匹配协议在 `src/matchsvr/protocol/`，组队协议在 `src/teamsvr/protocol/`，
客户端消息在 `com.protocol.match.proto`、`com.protocol.team.proto`。
算法与 WAL 位于各服务的 `logic/`；对应单元测试位于服务的 `test/`。
新增 RPC 和配置字段分别使用[RPC](../development/add-rpc-task)、
[服务端配置](../development/server-config)指南。
