# 固定容量 faction 的 robot 用例

这组用例驱动真实 Lobby → Matchsvr → Orbit 流程。运行前须生成并加载匹配配置、创建角色并启动相关服务。
每次使用没有队伍、匹配或对局状态的完整新账号区间；不要把重复次数设为大于 1。

## 配置规则

匹配不再读取成局模板。每个 Unit 都属于一个 faction：

- 玩家禁止撮合队友：独占 faction，容量为 Unit 人数。
- 玩家允许撮合队友：faction 容量固定为池的 `faction_user_max_size`。
- `faction_add_rule=0`（NONE）：不限制阵营目标容量之间的关系。
- `faction_add_rule=1`（队伍匹配人数）：加入的目标容量必须等于房间现有最大 faction 容量。
  补位 Unit 按池容量比较，不按当前 Unit 人数比较；空房由首个 Unit 建立容量。
- 所有非空 faction 都满员，并满足当前规则的 `min_user_cout_limit` 和
  `min_faction_count_limit` 后，才进入确认。平均和优先策略都不能提前启动未满员 faction。
- `unit_max_size` 限制 Unit，`faction_user_max_size` 限制 faction；两者必须分别配置。
- 加入仍检查地区、匹配参数、屏蔽关系、关卡交集和池硬上限，不推算未来成局组合。
- NONE 切换成 SAME_AS_TEAM 的收紧配置不在支持范围内。

## 用例前置条件

| case_config | 池策略 | 玩家补位 | 阵营容量上限 | 最大阵营数 | 最大总人数 | faction_add_rule | 开局最小人数 / 阵营数 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| matching_balanced_solo_3v3.conf | 平均 | 允许 | 3 | 2 | 6 | 1 | 6 / 2 |
| matching_priority_solo_3v3.conf | 优先 | 允许 | 3 | 2 | 6 | 1 | 6 / 2 |
| matching_priority_mixed_2v3.conf | 优先 | 双人禁止、单人允许 | 3 | 2 | 5 | 0 | 5 / 2 |
| matching_balanced_solo_1v1.conf | 平均 | 禁止 | 3 | 2 | 6 | 1 | 2 / 2 |
| matching_priority_solo_1v1.conf | 优先 | 禁止 | 3 | 2 | 6 | 1 | 2 / 2 |

关卡必须指向满足上述条件的池，不要修改共享规则来迁就某个用例。
玩家补位枚举与池策略枚举不同：`matching_level_select` 的补位参数 1 表示禁止，2 表示允许。
组队用例由队长选关并发起匹配，队员通过队内同步和心跳订阅。

同一个 SAME_AS_TEAM 规则可以让禁止补位的单人形成 1v1，让目标容量为 3 的阵营形成 3v3。
但允许补位的两个单人不会再随等待时间自动变成 1v1；两者的目标容量仍为 3，必须补满。
等待时间窗口可以放宽最小人数、最小阵营数和数值规则，不能降低已有 faction 的固定容量。
如果最小人数从 6 降到 2，两个已满员的独占单人阵营可以由 tick 触发成局，两个未满的三人阵营不可以。

## 执行与验收

在 `D:/DB/server/build/publish/robot/bin` 用 `.\standalone.ps1` 启动重新构建后的 robot：

```text
run-case-file "D:/DB/server/src/robot/case_config/matching_balanced_solo_3v3.conf" 1
run-case-file "D:/DB/server/src/robot/case_config/matching_priority_solo_3v3.conf" 1
run-case-file "D:/DB/server/src/robot/case_config/matching_priority_mixed_2v3.conf" 1
```

规则窗口和人数区间须覆盖测试规模；测试不涉及数值筛选时，不要配置阻止玩家共存的条件。
所有玩家须路由至同一 Matchsvr。等待上限应覆盖服务器搜索及创建战斗所需时间。
修改 Excel 后须生成并加载资源，不能只修改表格。

`matching_wait_success` 只在 FINISHED 时成功；确认中、创建战斗中继续等待，取消或失败立即报错。
Lobby 自动确认，不额外发送客户端确认。
最终必须验证所有玩家处于同一对局及各自 faction；自动等待成功本身不能证明分组正确或客户端已加载场景。
