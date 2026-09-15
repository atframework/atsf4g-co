# 组队服务

组队包含房间和匹配子服务，客户端接入由 lobbysvr 负责。

## teamsvr-room

组队房间服务。管理队伍，队伍成员并发操作时作为协调者。

当前 Room 行为、单元测试范围、历史协议数据和构建执行入口见 [Room 单元测试](test/README.md)。

## teamsvr-match

组队匹配服务，用于队伍匹配、队伍推荐操作。当前搜索 action 仍是占位实现。
