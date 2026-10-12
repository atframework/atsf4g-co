# Matchsvr 匹配与等待室

Matchsvr 以 Unit 为最小匹配单位。单人和队伍都使用同一条匹配流程，Unit 的成员不会被拆到不同 faction 或战斗房间。

普通匹配负责撮合、确认和创建 Orbit 房间。等待室在初始初始化成功后继续向同一 Orbit 房间加入 Unit，不重新创建 DS，也不重新划分已有 faction。

## 完成与职责边界

当前 Match 与 Orbit 的交接以 `SSOrbitUserInitReq` 的结果为准：RPC 成功且 `SSOrbitUserInitRsp.result_code == 0`，即认为提交的 Unit 加入成功。Match 不等待玩家实际连接 DS，不接收第二次连接完成通知。

初始调用可以一次提交全部初始 Unit。调用成功后这些 Unit 全部匹配完成；创建失败、初始化失败或初始创建超时，沿用普通匹配的失败流程，将匹配房间解散，不开放等待室。

这里的解散指 Match 本地房间和匹配占用结束。DS 初始化失败后的处理、玩家退出和结算由 Orbit 与原业务流程负责。本模块不发送 DS 踢出、撤销 token、关闭 DS 或结算命令。

## 普通匹配流程

1. Lobby 创建稳定的 Unit ID，提交玩家、可选关卡、匹配参数和订阅路由。
2. Match 校验 Unit、操作者、关卡配置和重复匹配占用。
3. 候选按 `level_type`、`region`、`battle_version`、`matching_pool_id` 分桶；优先尝试同桶等待室，没有合法候选才进入普通撮合。
4. 普通撮合维护关卡交集及固定容量的 faction。达到普通成局条件后进入确认。
5. 初始确认全部完成后创建 Orbit 房间，收到 room-ready，再初始化初始用户。
6. 初始初始化成功后，只有等待室配置有效、当前时间未到截止且初始人数未满，才开放等待室。否则按普通流程完成房间。

普通搜索中的 Unit 也会优先尝试等待室。每次 tick 在普通房间成局和跨房迁移前处理等待室候选；已经进入普通确认或创建战斗阶段的 Unit 不再迁入等待室。

### 请求入口与周期调度

`check_matching` 只校验请求、更新 Unit 心跳和订阅 ACK，并返回当前状态。心跳不触发等待室补人、普通成局、跨房迁移或等待室超时清退。

`orbit_room_ready` 完成初始用户初始化、发布初始 Unit 的完成事件，并在满足条件时登记等待室。回调不立即搬运其他排队 Unit；后续 tick 先处理等待室，再处理普通成局和迁移。

新 Unit 仍由 `create_matching` 按原有创建路径即时接纳，并优先选择等待室。确认、初始化发送及异步返回时仍检查绝对截止，不依赖下一次 tick 才拒绝过期操作。

### 模块接口的校验职责

| 接口所属对象 | 负责的校验与操作 |
| --- | --- |
| Manager 请求入口 | 请求格式、操作者属于指定 Unit、重复匹配占用、订阅路由合法性；组织候选评估和提交 |
| Logic 规则评估 | 当前配置下的匹配资格、屏蔽关系、关卡兼容和 faction 落点；不修改 Room 或发布事件 |
| Room 写入接口 | 成员唯一性、固定 faction 容量、预留人数和关卡交集；写入后同步更新计数及成员关系 |
| Unit 确认与订阅接口 | 只更新该 Unit 的成员确认、订阅路由、ACK 和 WAL；确认不会遍历其他 Unit |
| 异步回调 | 等待前后分别检查当前归属、状态、事件 ID 和绝对截止时间，拒绝已经失效的结果 |

创建流程先用 `prepare_subscribers` 校验并保存临时路由结果，再在原有绑定和视图更新时点调用 `initialize_subscribers`。准备阶段不发送消息；初始化会立即发送当前 Unit 视图，不能为减少校验而提前发送默认视图。准备结果只能用于生成它的 Unit，独立订阅请求仍完整校验输入。

规则层的候选筛选与 Room 的提交校验有不同用途。Room 写入接口还用于重新排队和迁移回滚，不能假设所有调用都已通过规则评估。等待室规则检查仍使用当前配置，不能复用最初创建时的人数限制结论。

迁入等待室时，Manager 同步收集、评估并提交同一组 Unit 对象，不再按 ID 重复查找。确认批次由 Room 在确认全员接受后一次性释放并返回；返回的是清除批次标记前的完整成员列表，Manager 据此逐 Unit 启动入场。

## 等待室流程

Room 使用 `EN_MATCHING_ROOM_STATUS_WAITING` 表示等待室开放。后补 Unit 沿用以下状态：

```text
SEARCHING -> CONFIRMING -> CREATING_BATTLE -> FINISHED
                  |              |
                  +--> CANCELLED / TIMEOUT / FAILED
```

后补的 `CREATING_BATTLE` 表示正在向已有 Orbit 房间初始化用户，不代表再次创建 DS。

接纳 Unit 时锁定目标 faction 并预留整队人数，立即进入确认，不等待等待室满员。确认成功后一次提交整个 Unit；初始化成功只完成该 Unit，Room 继续接纳其他 Unit。

等待室沿用初始房间已经选定的关卡。新 Unit 必须接受该关卡，并满足等待室自身的范围、屏蔽关系、容量和匹配参数规则。

## faction 的两种加入方式

已有 faction 的 ID、容量和已有成员归属保持不变。允许新增 faction，新 faction 的 ID 单调分配，不因撤销而复用。

| 加入方式 | 确认范围 | 有成员拒绝或确认超时时 |
| --- | --- | --- |
| 补入已有未满 faction | 当前 Unit 独立确认 | 整个 Unit 退出；其他 Unit 不受影响 |
| 多个 Unit 共同新增 faction | 固定该 faction 的创建成员，全部确认后才分别提交初始化 | 拒绝或超时 Unit 退出；其余创建 Unit 恢复搜索；撤销该次 faction |

新 faction 共同确认期间不再追加创建成员。创建确认释放后，后续补入按已有 faction 的独立 Unit 规则处理。初始化阶段每个 Unit 独立成功或失败，不回退已经成功的其他 Unit。

普通搜索中已经形成的完整 faction 可以整体迁入等待室，保持其容量及成员。普通搜索中的不完整 faction 以单 Unit 为迁移单位。所有迁移都不拆分 Unit。

匹配池允许补人也不能覆盖 Unit 自身的禁止拼队设置。禁止拼队的 Unit 只能独占与自身人数相同容量的新 faction。

## 容量与退出

等待室同时维护成功人数和预留人数：

```text
房间成功人数 + 未完成 Unit 预留人数 <= 匹配池的战斗房间人数上限
faction 成功人数 + 未完成 Unit 预留人数 <= faction 固定容量
```

预留占满时不再接纳，但不会仅因预留占满而永久关闭。截止前 Unit 拒绝或初始化失败会释放其本地预留；成功人数达到上限时等待室永久关闭。

成功 Unit 的玩家退出 DS，不扣减该等待室成功人数，不腾出位置，也不重新开放已关闭的等待室。退出后可使用新的 Unit ID 再次匹配同一等待室，也可匹配其他房间；Match 不等待旧 Room 回收或匹配侧结算。

再次加入同一等待室仍须满足等待室规则、剩余容量和截止时间，并按新 Unit 预留人数，成功后增加成功人数。成功成员数据继续参与规则比较，但不作为玩家重复匹配占用；尚未完成入场的 Unit 仍禁止玩家重复加入。Match 不检查玩家实际退出 DS 的状态。

## 绝对截止时间

等待室直接使用 `SSMatchingOrbitRoomReadyReq.end_join_timepoint`。它是绝对时间，不是持续秒数，也不从初始初始化成功时重新起算。

进入候选、接纳、确认、初始化发送、重试及 await 返回后提交结果，都要求 `now < end_join_timepoint`。`now == end_join_timepoint` 已经到期。

截止时所有尚未完成的后补 Unit 按超时结束，不再确认、提交或重试。已经发出的 RPC 无法由 Match 撤回；截止后返回的成功结果不会复活 Unit、增加成功人数或发布 `BATTLE_READY`。远端如何处理已经收到的数据归 Orbit。

tick 负责扫描并记录等待室超时；确认和初始化入口也会在执行操作前处理已到期房间。截止后、这些入口尚未执行时，心跳可以返回此前的状态，但不能据此继续入场。

例如截止为 12:00:30，后补请求在 12:00:29 发送、12:00:31 才成功返回，Match 将该 Unit 作为超时处理。若已经在 12:00:29 完成成功提交，后续截止不影响其完成状态。

后补确认期限取池确认超时和等待室截止的较小值。初始创建仍使用原创建超时；初始初始化成功时已到截止，则不开放等待室。

## 配置

配置源是 [`resource/ExcelTables/Matching.xlsx`](../../resource/ExcelTables/Matching.xlsx)，协议源是 [`com.struct.matching.config.proto`](../server_frame/protocol/public/protocol/config/com.struct.matching.config.proto)。保持工作簿中的既有字段拼写。

### 匹配池

| 字段 | 用途 |
| --- | --- |
| `is_has_waiting_room` | 启用等待室；默认 false |
| `waiting_room_rule_id` | 引用一条 `ExcelMatchingWaitingRoomRule` |
| `is_waiting_room_faction_can_joined` | 是否允许向已有未满 faction 补 Unit |
| `max_user_cout_limit` | 战斗房间成功与预留人数的总上限 |
| `max_faction_cout_limit` | faction 数量限制 |
| `unit_max_size` | 单 Unit 人数上限 |
| `faction_user_max_size` | 可拼队 faction 的固定容量 |
| `confirm_timeout_seconds` | 确认超时；实际确认截止不晚于等待室截止 |

### 等待室加人规则

`等待室加人规则` 工作表独立导出 `matching_waiting_room_rule.bytes` 和 JSON，由 `ExcelMatchingWaitingRoomRule` loader 加载。

| 字段 | 用途 |
| --- | --- |
| `id` | 单条规则 ID |
| `faction_add_rule` | faction 容量关系限制 |
| `region_limits` | 允许的地区 |
| `rules` | 段位、角色等级等已有基础规则条件 |

每个池引用单条规则，不把该表解释为按人口选择的规则组。等待室规则不配置相对时间窗口，入场时间只受 `SSMatchingOrbitRoomReadyReq.end_join_timepoint` 的绝对截止时间限制。不满足规则时不退回普通撮合规则，也不自动切换相邻 ID。

[`get_matching_waiting_room_rule()`](../server_frame/config/include/config/excel_config_matching_index.h) 在配置无效时记录错误并返回空。启用状态下的空规则必须拒绝等待室接纳，不能按无限制规则处理。关闭开关的池沿用普通匹配流程。

当前等待室规则和池限制通过当前配置组读取；已有 faction 容量仍固定。热更新时不得假设存量 Room 已保存全部旧配置，修改开关、规则或房间上限前应考虑存量等待室。

## Unit 身份、WAL 与 Room 生命周期

`unit_id` 标识一次匹配，Unit 在撮合房间间迁移或确认回退时保持 ID、对象、WAL publisher 和事件序列。玩家主动开启下一轮匹配时使用新的 Unit ID。

`last_event_id` 表示 Unit 最新的 WAL 事件 ID。处于 `CONFIRMING` 时，它就是当前 `CONFIRM_REQUIRED` 的事件 ID：部分玩家确认不会发布新事件；重新搜索或开始初始化时先退出确认状态，再发布状态变化。完整快照可由确认状态和该 ID 恢复确认操作，不另存确认事件字段或计数器。

`SSMatchingConfirmReq.last_event_id` 保存发出请求时确认态视图的事件 ID。Match 要求仍处于确认状态，且请求 ID 非零并等于 Unit 当前 `last_event_id`，普通匹配与等待室一致。`acknowledge_event_id` 仍表示已完成业务消费的最大连续事件，不能代替正在执行的确认事件 ID。等待室初始化任务保存发布初始化事件后的 `last_event_id`，提交前核对该 ID、Room 和 Unit 归属、状态及截止。部署时需要同步更新 Match 和 Lobby 的匹配协议及消费者。

Unit 成功后从 Room 活动集合和活动玩家索引移除，按自身终态时间保留最终视图与 WAL。Room 保存已成功成员摘要、faction 和人数，用于继续匹配；这些信息不依赖已完成 Unit 的 WAL 一直存在。

Room 到期或满员后结束本地未完成 Unit，进入终态保留。Room 回收不能再次清理已经释放的玩家索引，也不能影响玩家新一轮匹配。

Lobby 对 `CONFIRM_REQUIRED` 执行当前 Unit 的确认，对 `BATTLE_READY` 执行现有 Orbit 订阅。业务副作用完成后才推进 ACK；增量丢失或压缩后的完整视图仍能恢复副作用。Team 的匹配标志按 Unit 的结果结束，不等待 Room 关闭。

Lobby 用 `acknowledge_event_id` 记录已消费位置，`pending_acknowledge_event_id_` 记录异步确认成功后可提交的位置，`last_reported_acknowledge_event_id_` 记录已上报位置。确认 RPC 在调用栈中保存发出时的 `last_event_id`。重新搜索或新的确认事件使旧待提交位置失效；正常进入初始化或完成状态时保留它，不能仅因最新事件 ID 增长而丢弃成功回包。旧请求只能完成自己对应的 ACK，不能提交新确认事件。

## Orbit 协议范围

本实现复用 `SSMatchingOrbitRoomReadyReq.end_join_timepoint`、`SSOrbitUserInitReq` 和原 `result_code`。不修改 Orbit 服务、DS 协议或结算流程，不新增 reserve、commit、query、cancel、实际连接回报或 DS 撤销接口。

无等待室初始请求沿用 `is_last_one=true`。需要保留追加入口时，Match 初始和后补请求使用 false。等待室关闭后停止发送后补初始化；Match 本地结束不依赖向 DS 发送空封口请求或等待封口确认。

当前 Orbit 的重试和重复用户处理沿用原契约。Match 的确认事件关联和 WAL 防重只保证本地状态及通知不会被迟到回包重复推进，不承诺已经新增远端幂等机制。

## 实现入口与验证

| 文件 | 主要职责 |
| --- | --- |
| [`matching_manager.cpp`](service/logic/matching/matching_manager.cpp) | 候选优先级、索引、确认、Orbit 初始化、截止和回收 |
| [`matching_logic.cpp`](service/logic/matching/matching_logic.cpp) | 普通与等待室的规则判断、增量放置 |
| [`matching_room.cpp`](service/logic/matching/matching_room.cpp) | 固定关卡/faction、成功与预留人数、确认范围 |
| [`matching_unit.cpp`](service/logic/matching/matching_unit.cpp) | Unit 生命周期、确认事件关联、WAL 和最终视图 |
| [`matching_logic_test.cpp`](test/matching_logic_test.cpp) | 受控时间和 RPC mock 场景 |
| [`matching_waiting_rule_test.cpp`](test/matching_waiting_rule_test.cpp) | 独立配置、时间窗、固定 faction、人数预留与完成摘要 |

修改时至少验证开关关闭、初始失败、绝对截止相等及跨 await 到期、新请求和已有搜索候选优先级、两类 faction 拒绝范围、最后名额竞争、Unit 完成后 Room 继续存在、WAL 回收和旧消息隔离。通知验证需要同时检查内容与实际投递次数。

测试应使用工程 RPC 单元测试目标与离线 runtime。生成配置后要检查实际 bytes/JSON 字段；生成器退出成功本身不能证明工作簿列被正确消费。测试结果以本轮实际构建和执行日志为准，本文件不记录未经执行的通过结论。
