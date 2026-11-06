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
   组队匹配场景可参照 `matching_team_3v3.conf`。等待室用例见 `matching_waiting_cases.md`，但其测试池和
   关卡不属于当前默认工作簿，必须先按该文件准备额外 fixture，不能直接在默认资源上运行。

只调整已有匹配参数时修改 Excel 和 values 即可。新增算法或撮合语义需要实现业务逻辑。
匹配结果后的战斗进程管理见[Orbit](orbit)。

### 等待室与状态

大厅把 `DUserMatchingData` 持久化到 `table_user.matching_data`，其中包含 `level_data.level_select`；
`user_matching_manager::init_from_table_data` 和 `dump` 分别负责恢复与写出。

等待室在初始 Orbit 用户初始化成功后接纳后补 Unit，不重新创建 DS，也不重排已有 faction。后补流程同时受
房间与 faction 的容量预留限制，并在接纳、确认、初始化、重试和异步返回提交时检查绝对截止
`end_join_timepoint`；迟到成功不能恢复已超时 Unit。后补初始化失败会关闭整个等待室，已成功的其他 Unit
不回退。成功玩家可以使用新 Unit ID 匹配其他房间，但旧等待室按已接纳 Unit 和玩家身份拒绝重入。

Match 当前的 Orbit EndJoin/UserQuit action 仍是返回成功的占位实现，不能据此认为 DS 结算已经完成。
完整状态与 WAL 约束见 `src/matchsvr/README.md`。

## 快速上手：组队

1. 启用 `teamsvr-room`；队伍进入战斗匹配时配套 `matchsvr`。
   修改 `resource/ExcelTables/Team.xlsx` 的组队类型配置并导出。
2. 消费方 `USE_SERVICE_SDK` 加入 `team-common-sdk`、`team-sdk-room`。
   房间接口位于 `src/teamsvr/sdk/room/rpc/team/team_room_client_api.h`。
3. 参照 `src/lobbysvr/service/logic/team/user_team_manager.h/.cpp` 接入成员、邀请、队长和状态同步。
4. 用测试用户验证创建队伍、邀请/加入、离开，以及所需的匹配流程；检查客户端状态与通知。

邀请与主动加入申请是两条流程。主动申请使用 `team_send_join_request`，审核使用
`team_accept_join_request` / `team_reject_join_request`；队伍类型的 `EN_TTDF_DISABLE_INVITATION` 和
`EN_TTDF_DISABLE_APPLY` 分别控制邀请与申请。需要同时验证 Lobby 缓存、上行 merge，以及下行全量/增量通知
在实际 flush 边界的必发、去重和恢复行为。

`teamsvr-match` 的 `TeamMatchService.search` 尚未实现寻找队友逻辑；它与队伍进入 `matchsvr` 撮合的流程不同，
不属于上述最小接入。

## 定制与详细设计

匹配协议在 `src/matchsvr/protocol/`，组队协议在 `src/teamsvr/protocol/`，
客户端消息在 `com.protocol.match.proto`、`com.protocol.team.proto`。
算法与 WAL 位于各服务的 `logic/`；对应单元测试位于服务的 `test/`。
新增 RPC 和配置字段分别使用[RPC](../development/add-rpc-task)、
[服务端配置](../development/server-config)指南。
