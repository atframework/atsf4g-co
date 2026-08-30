# lobbysvr 用户组队单元测试执行计划

> 状态：2026-08-30 全部用例已落地并通过（`lobbysvr_user_team` 组 68/68，lobbysvr suite 76/76）。本文只
> 描述应保留的最新契约，不记录历史方案；§6 记录执行期关闭的实现缺陷，§7 记录完成证据。
>
> 被测范围：`src/lobbysvr/service/logic/team/` 的 `user_team_manager`、`user_team` 和组队 CS task action。
> 服务端事实源：当前工作区的 `src/teamsvr/service/room/logic/room/team_room.cpp`、组队 protobuf、
> `src/teamsvr/TEAM_ROOM_TEST_PLAN.md`。计划状态不能代替测试执行证据。

## 1. 目标、边界和验收口径

本计划验证从 teamsvr-room 权威状态到 lobbysvr 本地缓存，再到客户端脏数据的完整链路：

```text
CS 请求
  -> lobbysvr task action / user_team_manager / user_team
  -> TeamRoomService SS RPC
  -> teamsvr-room 写 DTeamAction 或 DTeamStorage
  -> 队伍频道 DTeamAction / 快照
  -> lobbysvr user_team 缓存
  -> SCUserDirtyChgSync(snapshot / increase)

teamsvr-room DTeamMemberAction
  -> 玩家个人频道
  -> lobbysvr user_team_manager
  -> 自己的 pending 邀请/加入请求或当前队伍索引
```

验收必须同时检查：

1. 真实入口的结果码；
2. manager 索引和 `user_team` 缓存的增删改查；
3. 过期、重复、乱序、快照覆盖后的终态；
4. 发往 teamsvr-room 的 SS RPC 次数和完整 payload；
5. 发往客户端的 snapshot/increase 类型、完整业务字段和必须裁剪的内部字段；
6. 对象移除后的 table 数据、迟到事件幂等性和 dirty handle 清理。

单向 SS/CS 调用未匹配时可能被 mock 记录后静默丢弃，返回成功不能证明通知已经发生。所有必须发送的
RPC、个人通知和客户端脏推送都要捕获实际调用并检查 payload；所有禁止发送的分支要检查调用数不增加。

本计划是离线单元测试范围。真实 DTMQ 进程、跨进程故障转移和 DB 持久化属于集成测试；但 lobbysvr
自身的 table dump/init 往返仍属于本计划的数据完整性范围。

## 2. 权威数据和本地缓存

### 2.1 状态归属

| 状态 | 权威来源 | lobbysvr 本地结构 | 可观察结果 |
| --- | --- | --- | --- |
| 当前/退出中队伍 | `joined_team`、create 响应、table、频道销毁 | `team_group_`、`team_index_` | `get_team_by_*`、table dump、`user_get_info` |
| 成员 | `DTeamStorage.member`、`add_member/remove_member/member_update/member_set_role/election_captain` | `cached_members_` | `DUserTeamSnapshot.snapshot.member`、`unpacked_member_data`、increase |
| 队伍配置/共享数据 | `DTeamStorage.configure/shared_team_data`、`team_update` | `cached_configure_`、`cached_team_shared_data_` | snapshot 的 configure、解包后的 `shared_team_data`、increase |
| 队伍级待处理邀请 | `DTeamStorage.pending_invitation`、邀请类 `DTeamAction` | 按过期时间 list + invitee 索引 | snapshot/increase |
| 队伍级待处理加入请求 | `DTeamStorage.pending_join_request`、加入请求类 `DTeamAction` | 按过期时间 list + requester 索引 | snapshot/increase |
| 自己收到的邀请 | 个人频道 `invited/reject_invitation` | manager 按过期时间 list + team_key 索引 | `get_pending_invitation`、CS approve/reject |
| 自己发出的加入请求 | 个人频道 `apply_join_request/reject_join_request` | manager 按过期时间 list + team_key 索引 | `get_pending_join_request`、重复申请预检 |
| 个人频道消费水位 | 玩家个人频道 sequence | `processed_private_chat_channel_sequence_` | table dump/init、重放去重 |
| 队伍频道消费水位 | 快照 `saved_action_sequence`、消息 sequence/hash | `channel_saved_sequence_`、`last_applied_action_*` | 增量去重、heartbeat payload |

两个 pending 层不能混淆：

- `user_team` 的 pending 是“本队所有候选成员”，key 是 invitee/requester；
- `user_team_manager` 的 pending 是“当前玩家自己的邀请/申请”，key 是 team_key。

两层都必须测试 create/read/update/delete、过期前缀清理、同 key 刷新和索引/list 同步，不能只通过
protobuf 条目数量间接推断。

### 2.2 teamsvr-room 个人通知的完整 payload

以下表格以 `team_room::apply_*` 和 `team_room::remove_member` 当前实现为准。每个用例使用非默认、可区分值，
逐字段比较；不得只检查 team_id 或 user_id。

| `DTeamMemberAction` | 接收者和来源频道 | 服务端实际 payload | lobbysvr 动作 |
| --- | --- | --- | --- |
| `invited` | invitee 的 `invitee_private_channel` | 完整邀请身份、私有频道、来源、开始/过期时间；先清空请求中的 admission，再填充当前队伍所有 PUBLIC `team_admission_data`，并为每个成员填 `user_key` 和其全部 PUBLIC `member_admission_data`；MEMBER 数据不得泄漏 | 完整保存到自己的 invitation cache |
| `reject_invitation` | 本地 pending 记录中的 invitee 私有频道 | pending 邀请的身份、频道、来源和时间；`team_admission_data/member_admission_data` 清空 | 删除自己的 invitation cache |
| `apply_join_request` | requester 的 `requester_private_channel` | room 归一化后的完整 `DTeamJoinRequest`：team_key、requester、私有频道、来源、client_version、router、expired_timepoint、member_admission_data | 完整保存到自己的 join-request cache |
| `reject_join_request` | 本地 pending 记录中的 requester 私有频道 | pending 加入请求的身份、频道、来源、client_version、router、过期时间；`member_admission_data` 清空 | 删除自己的 join-request cache |
| `joined_team` | 被批准的 invitee/requester 私有频道 | `team_key/user_key/team_channel/captain_user_key/user_role`，不带 admission | 注册/恢复 `user_team`，订阅 `team_channel` |
| `remove_member` | 被移除成员的 `user_channel` | `team_key/user_key/remove_member_reason` | 将队伍转入退出收敛；真实 reason 必须保留 |

约束：create 不产生 `joined_team`，由 lobbysvr 使用 create 响应的 `team_key/room_channel` 直接注册创建者；
room 的 admission 到期清理不发个人取消通知，lobbysvr 按 `expired_timepoint <= logical_now` 自行删除。

### 2.3 队伍频道和客户端投影

| teamsvr-room 权威数据 | lobbysvr 缓存要求 | `SCUserDirtyChgSync` 要求 |
| --- | --- | --- |
| create/compact 的 `DTeamStorage` | 清空旧成员、共享数据、配置和两类队伍级 pending 后权威重建；已过期/无效 admission 不进入缓存；成员表决定 `is_member_` 和自己的 role | 一条 snapshot；成员内部频道/router/ack 和原始 `shared_member_data` 清空；成员共享数据转到 `unpacked_member_data`；原始 `snapshot.shared_team_data` 清空并转为解包后的 `shared_team_data`；pending 私有频道/router 清空 |
| invitation 批准的 `add_member` | 版本/路由/频道/加入及心跳时间/role/source/shared data 完整消费；重复 add 保留更早 joined_timepoint | increase 保留 user_key、时间、role、source、client_version；剥 user_channel/router/ack/原始 shared data，shared data 解包到 `OneAction.shared_member_data` |
| join-request 批准的 `add_member` | 同上；shared data 来自申请记录的 member_admission_data | 同上 |
| `remove_member` | 删除目标成员；目标为自己时置 `is_member_=false`、role=GUEST | increase 必须保留 team_key、user_key、真实 reason |
| `member_update` | 已存在成员仅用非空 client_version 覆盖，shared data 按 key 合并；未知成员不创建幽灵缓存 | 剥内部频道/router 和原始 shared data，保留 user_key/client_version，shared data 解包后下发 |
| `member_set_role` | 更新目标成员 role；目标为自己时同步权限 role | 完整 action increase |
| `election_captain` | 新队长必须已在成员缓存；默认 role=GUEST 时继承原队长 role，原队长缺失才回退 OWNER；显式 role 使用事件值；旧队长降 NORMAL | 完整 action increase；缓存和 action 投影终态一致 |
| `team_update` | configure 整体覆盖；shared data 按 key 合并并执行 matching 本地行为 | 完整 action increase；队伍共享数据仍在 action 中以 Any 形式下发 |
| invitation/join-request `add/approve/reject` | add upsert，approve/reject delete；add_member 连带删除该用户的两类 pending | invitation 清私有频道；join request 清私有频道/router；`add_join_request.member_admission_data` 转为 `OneAction.shared_member_data` |
| `destroy_team`、频道 destroy/on_destroyed | 清空全部 `user_team` 缓存并从 manager 移除 | 客户端先收到 `destroy_team` increase；订阅失败/仅有频道 destroy 时合成等价 destroy action；下发后注销 dirty handle，不允许迟到 push |

快照和增量是不同客户端契约，不能用一次 `dump()` 同时冒充两者。snapshot pending 保留可见的 admission
原始数据；increase 对 `add_join_request` 使用解包后的成员数据。所有列表比较按业务 key 归一化，不能依赖
unordered_map 的输出顺序。

队伍频道的 approve/reject action 与个人频道回执也不能混淆：room 写入队伍日志的是规范化后的完整 pending
记录，lobbysvr 从队伍级缓存删除条目时仍要按 §2.3 完整投影该 action；只有个人 `reject_invitation` 和
`reject_join_request` 回执会由 room 主动清空 admission 数据。

## 3. 被踢出和消息缺失的 P0 收敛契约

被移除者已经订阅队伍频道，因此个人通知不是唯一信息源。必须分别覆盖以下三条获知路径。

### 3.1 个人通知送达

覆盖两种到达顺序：

1. 队伍频道 `remove_member(self)` 先到：先断言 self 成员缓存删除、role=GUEST、客户端收到包含真实 reason
   的 increase；再到个人 `remove_member` 后 manager 可移除该队伍。
2. 个人 `remove_member` 先到：当前队伍转入 pending-to-exit，缓存暂时保留；随后队伍频道
   `remove_member(self)` 到达并完成客户端 increase；minute refresh 收编队伍。该子场景在注入队伍事件前推进到
   exit retry 边界，捕获补发 remove 请求并断言 reason 等于个人通知中的真实 reason，用来锁定 reason 没有在
   manager 层丢失。

共同终态：`team_index_`、current/pending-to-exit、table group 均无该队，全部队伍级缓存随对象释放，dirty
handle 注销；迟到的重复个人通知或队伍事件不重建队伍、不重复产生客户端脏数据。

### 3.2 个人通知丢失，队伍频道 `remove_member(self)` 送达

用例初始快照必须同时包含 self、另一成员、队伍共享数据和两类 pending，以便检查数据完整性：

1. 注入 `remove_member(self)`，断言客户端立即收到带 `team_key/user_key/remove_member_reason` 的 increase；
2. 在对象尚待生命周期收编时，dump 已无 self，自己的 role 已回到 GUEST；其他成员、共享数据和 admission
   不得被误删或污染；
3. 通过逻辑时钟推进超过实际配置的 `wait_add_member_timeout`（默认 5s，不得写死旧的 30s）并调用 minute
   refresh，断言 manager 索引、分组和 table 数据最终清空；
4. 再注入迟到的个人通知，断言幂等且不产生二次退出循环。

退出请求的瞬时发送次数不是此兜底的业务 oracle；必须断言的是客户端已获知、缓存终态正确且清理后不再重试。

### 3.3 个人通知和 remove 日志都丢失，只收到权威快照

先建立含 self 和丰富缓存的旧快照，再注入更高 custom-data sequence 的新快照且成员表不含 self：

1. 新快照必须先清空旧缓存再重建，旧 self、旧共享 key、旧 admission 均不得泄漏；
2. 客户端收到 snapshot，成员表不含 self，且 snapshot 的成员、配置、共享数据和 pending 与新快照逐项一致；
3. 已超过 `wait_add_member_timeout` 时，在本次快照回调完成下发后移除队伍；未超过时保持对象直到 minute
   refresh，但不得恢复 self；
4. 对照用例的新快照仍包含 self，必须保留队伍且不得误判超时。

这两个“个人消息未送达”用例是 P0 门禁，不能由一个参数化用例合并，也不能只断言 team 指针为空。

## 4. 测试夹具和驱动方式

### 4.1 目标和源码

复用 `${PROJECT_NAME}-lobbysvr-unit-test`、`atfw::testing::runtime`，运行时 features 为 `{ss, cs}`。
现有 `src/lobbysvr/test/lobbysvr_test_user_team_manager.cpp` 已有 7 个 `lobbysvr_user_team` 用例。

`src/lobbysvr/test/CMakeLists.txt` 已显式编入本目录 12 个 `task_action_team_*.cpp` 和全部 `team_test` 源文件，
CS 请求层用例经真实 dispatcher 入口执行。不要新建第二个 service test target。

用例增长后将通用 helper 下沉到 `lobbysvr_test_user_team_common.h`，但场景输入和关键期望保留在 case 中。

### 4.2 真实入口

- 个人频道和队伍频道都经
  `rpc::dtmq::client_subscriber::global_receive_channel_event(kDtmqProxyNodeId, SSChannelEventSync)` 注入；
- 快照使用真实 `DTeamStorage` 打包到 channel custom_data，增量使用合法 sequence/hash chain；
- CS task action 使用生成的 dispatcher/typed action 入口，不直接调用 manager 绕过登录、权限和参数预检；
- manager/user_team 单元用例可在真实 runtime task 内调用对应生产方法；
- SS mock 使用生成的 `rpc::team::team_api::packer::get_full_name_of_*()`，捕获 typed request 和 client_result；
- CS mock 绑定 session，解析 `user_dirty_chg_sync` 并按 team_key、dirty 类型和 action case 收集 payload。

### 4.3 同步和时间

- 不把固定 `pump_rounds(N)` 当作业务正确性证据。新增 `pump_until(predicate, hard_limit)`：每轮 pump 后检查
  “缓存到达期望状态”“指定 RPC 被捕获”或“dirty payload 已收到”等可观察条件；hard limit 只防挂死；
- 不断言“每个 action 恰好一条 CS push”。允许同一批 action 合并，断言全部期望 action 恰好出现且 payload
  正确、没有额外 action；
- 使用 `time_utility::set_global_now_offset/update` 推进逻辑时间，显式调用 second/minute refresh；每例 teardown
  必须 reset offset；
- 每个前置断言失败后先完成 `test.stop()` 再 return；每例最后断言 `test.stop()==0`，让未消费的 SS expectation
使测试失败；
- dtmq 订阅管理和 mock WAL 是进程级状态且频道水位不回退：同一测试进程内不得复用 user/team/channel id，
  各测试文件独占 id 段（现状：manager 30001-30006、cs 30010/30090+、admission 30011-30014、
  cache 30021-30024、kick 30031-30039、robust 30041/30051-30059、lifecycle 30101-30110；新增用例先查重）；
- 注入快照会重置该频道 WAL：快照之后的增量日志 hash 从 0 重新链（`channel_event_chain.hash_code` 归零），
  不得沿用快照前的 hash 续链；
- 个人频道业务回调按 Any 消息类型分发：type_url 不匹配的事件在订阅层被过滤，业务回调不触发、manager 水位
  不推进；只有 type_url 匹配但 value 损坏的事件才走“水位推进后忽略”路径；
- CS 用例每个 runtime 必须调用
  `handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice()` 注册处理器；
  `team_test::post_cs_request` 捕获的响应必须显式消费，否则 dispatcher 的 stale-response 清理会让用例失败。

## 5. 用例矩阵

截至 2026-08-30 下列矩阵全部落地并通过，各条目的实现用例见 §5.7 覆盖映射；§6 列出执行期关闭的
实现缺陷。新增用例时保留原 ID 语义，不要把多个不相关场景塞进同一个 case。

### 5.1 P0：成员、快照、退出和客户端投影

| ID | 状态 | 场景和关键断言 |
| --- | --- | --- |
| MEM-01 | 已覆盖 | 富字段快照权威重建：成员全部业务字段、configure、队伍/成员共享数据、两类 pending；内部字段只在客户端投影层裁剪，缓存更新不能污染源 fixture |
| MEM-02 | 已覆盖 | `add_member` 新增和重复 upsert；同 key 不重复成员，保留更早 joined_timepoint，较新的业务字段覆盖；self 首次加入只 flush 一次本端 shared data，并连带删除该用户的队伍级 invitation/join request |
| MEM-03 | 已覆盖 | `member_update` 的非空 client_version、共享数据按 key 后写覆盖/未出现 key 保留；空版本不回退；未知成员不创建缓存；dirty 投影剥内部字段并解包 shared data |
| MEM-04 | 已覆盖 | `remove_member(other)` 删除存在成员并下发完整 increase；重复删除/未知成员幂等，不损坏其他缓存 |
| ROLE-01 | 已覆盖 | `member_set_role` 更新他人与 self；self 同步 `cached_permission_role_`，权限判断随事件改变 |
| ROLE-02 | 已覆盖 | `election_captain` 显式 role、默认 role 继承原队长 role、原队长缺失回退 OWNER、旧队长降 NORMAL、同一人不误降、非成员目标不改缓存 |
| TEAM-01 | 已覆盖 | `team_update` configure 整体替换、shared data 同 key 合并、matching 行为；空/错误 Any 不进入解包缓存，合法未知模块不能被注册 handler 的存在与否误删 |
| DIRTY-01 | 已覆盖 | snapshot 投影逐字段完整且内部字段裁剪正确；比较使用业务 key，不依赖输出顺序 |
| DIRTY-02 | 已覆盖 | 除单独由 DEST-01 验证的 destroy 外，每一种 `DTeamAction` 的 increase 投影逐字段检查；批量/单条发送都只要求 action 集合和顺序契约，不锁死 CS push 数量 |
| KICK-01 | 已覆盖 | §3.1 两种个人通知/队伍事件顺序及共同终态 |
| KICK-02 | 已覆盖 | §3.2 个人通知丢失、仅 `remove_member(self)` 的客户端获知和最终缓存清理 |
| KICK-03 | 已覆盖 | §3.3 两种消息都丢失、仅快照无 self 的客户端获知和权威重建；含 self 对照组 |
| KICK-04 | 已覆盖 | 只有个人 remove 到达、队伍快照仍暂时含 self：pending-to-exit 重试/exit timeout 后收编，期间不把队伍作为 running team 导出 |
| DEST-01 | 已覆盖 | `destroy_team` action 必须先下发完整 destroy increase 再清缓存；channel destroy/on_destroyed 无 action 时合成等价通知；三条路径随后清 manager，低于新 create sequence 的旧 destroyed 回调忽略 |

### 5.2 P0：队伍级 invitation/join-request 缓存

邀请和加入请求各自执行同一组行为，但保留为两个独立 case，避免字段不对称被遗漏。

| ID | 状态 | 场景和关键断言 |
| --- | --- | --- |
| ADM-TEAM-01 | 已覆盖 | 快照加载多条有效/已过期/无效 key 数据；只保留有效项，完整字段不丢，按过期时间升序导出 |
| ADM-TEAM-02 | 已覆盖 | `add_invitation` 同 key 同 expiry 原位覆盖、expiry 变化重排；invitee 已是成员时忽略；team/source/time/admission 全字段保存 |
| ADM-TEAM-03 | 已覆盖 | `approve_invitation/reject_invitation` 删除 list 和索引；重复结果幂等；dirty 私有频道裁剪且 admission 投影符合 §2.3 |
| ADM-TEAM-04 | 已覆盖 | `add_join_request` 同 key覆盖/重排、成员忽略；requester/channel/source/version/router/expiry/admission 全字段保存 |
| ADM-TEAM-05 | 已覆盖 | `approve_join_request/reject_join_request` 删除 list 和索引；重复结果幂等；dirty channel/router/admission 投影符合 §2.3 |
| ADM-TEAM-06 | 已覆盖 | `add_member` 同时删除该 user 的 invitation 和 join request；删除一个 key 不影响其他 key |
| ADM-TEAM-07 | 已覆盖 | `expired_timepoint == now` 视为过期；分两段推进时间，只清理已过期前缀；refresh 后 list/index/dump 三者一致，且不伪造 room 取消事件 |

### 5.3 P0：自己的 pending invitation/join-request 缓存

| ID | 状态 | 场景和关键断言 |
| --- | --- | --- |
| ADM-SELF-01 | 已覆盖 | `invited` 按 §2.2 构造真实完整通知；manager 逐字段保存 PUBLIC 队伍/全成员快照，MEMBER 数据不泄漏；非本人 invitee 忽略 |
| ADM-SELF-02 | 已覆盖 | `apply_join_request` 保存 room 归一化后的全部字段；非本人 requester 忽略；send RPC 成功本身不提前插入缓存 |
| ADM-SELF-03 | 已覆盖 | 两类缓存的同 team_key 同 expiry 原位覆盖、不同 expiry 重排、不同 team_key 共存；通过 get 返回的最新完整内容和分段 cleanup 终态证明 list/map 同步 |
| ADM-SELF-04 | 已覆盖 | 无效 team_id、已过期、`expiry == now` 均不得插入 |
| ADM-SELF-05 | 已覆盖 | 个人 `reject_*` 删除对应 list/index，重复 reject 幂等，另一类/另一 team 不受影响 |
| ADM-SELF-06 | 已覆盖 | approve/reject invitation 上行 payload 完整；成功删除；普通业务失败保留以便重试；`DTMQ_CHANNEL_NOT_FOUND` 说明 room 已不存在，应映射错误并删除确定失效的本地记录 |
| ADM-SELF-07 | 已覆盖 | `joined_team`/create/登录恢复注册 team 时，必须删除同 team_key 的自己的 invitation 和 join request |
| ADM-SELF-08 | 已覆盖 | second refresh 分段清理两类过期前缀；清理后 get 返回空；重复申请预检不能把已过期但尚未 refresh 的记录当作有效 |
| ADM-SELF-09 | 已覆盖 | table dump/init 往返后，未过期的自己的 invitation/join request 与消费水位同时恢复，旧个人事件仍去重 |
| ADM-SELF-10 | 已覆盖 | 个人频道 sequence 小于/等于水位不重复处理，大于水位只处理一次；水位变化设置 manager dirty 并正确落地 |

`ADM-SELF-09` 采用的修复：`user_team_data` 持久化两类自己的 pending（`svr.local.table.proto`），dump/init 往返
恢复；水位保持落地语义，不依赖全量重放（重放会重新执行历史 `joined_team/remove_member` 副作用）。

### 5.4 P1：注册、切队、序列和生命周期

| ID | 状态 | 场景和关键断言 |
| --- | --- | --- |
| JOIN-01 | 已覆盖 | `joined_team` 的五个字段完整进入新队；订阅严格使用 `team_channel`；非本人通知忽略；snapshot ready 后才判定成员 |
| JOIN-02 | 已覆盖 | 同组 A→B：A 转 pending-to-exit 并发 reason=`IN_ANOTHER_TEAM` 的 exit；table 只保存 B/current 和 team_type |
| JOIN-03 | 已覆盖 | pending-to-exit 的 A 再收到 joined：A 恢复 current、B 退出，旧 A 不泄漏且恢复后不重试旧 exit |
| CREATE-01 | 已覆盖 | create SS 请求完整：team_id=0/zone、sender/channel、client_version/router、configure 保持当前默认空值（由 room 修订默认门槛）、两类初始 shared data；响应成功后无需 joined 即注册 OWNER/captain/self，输出 key 精确等于响应 |
| CREATE-02 | 已覆盖 | transport/client_result 失败不注册、不修改 output/index，错误码精确透传 |
| SEQ-01 | 已覆盖 | 快照 `saved_action_sequence=N` 后 `<=N` 日志不应用，`>N` 按 hash chain 应用；重复日志不重复 dirty |
| SEQ-02 | 已覆盖 | update-custom-data 快照覆盖旧缓存，快照回放期间 action 被 snapshot 吞并只下发最终 snapshot；实时 action 下发 increase |
| SEQ-03 | 已覆盖 | 高 create sequence 新代际快照后，旧 destroy/on_destroyed 乱序到达不删除新代际 |
| HB-01 | 已覆盖 | 成员且 ready 时 heartbeat 上报 team/user/router 和最新 sequence/hash；间隔内节流、达到边界再发 |
| HB-02 | 已覆盖 | 非成员、退出中、未 ready、destroyed 均不发 heartbeat |
| EXIT-01 | 已覆盖 | 主动退出上行 remove self payload 和 reason；仍为成员时进入 pending-to-exit，回环 remove 后收编 |
| EXIT-02 | 已覆盖 | retry interval 边界、exit timeout 边界和清理后停止重试；频道 destroyed 可直接移除 |
| DUMP-01 | 已覆盖 | manager table dump/init 恢复 team_type/current/channel/captain/role/个人水位；pending-to-exit 不落地；非法 team type/channel 不恢复 |
| DUMP-02 | 已覆盖 | `user_get_info(need_user_team)` 只导出 current 且非 exiting/destroyed 的队伍，并使用与 dirty snapshot 相同的字段裁剪契约 |

### 5.5 P1：CS 请求和上行 SS payload

以下用例必须在 CMake 编入 task-action 源码后通过真实 CS action 入口执行。每组至少选择一个未登录用例确认
`EN_ERR_LOGIN_NOT_LOGINED`；其余分支重点检查零上行或完整上行，不机械复制相同基类检查。

| ID | 请求流程 | 必测分支和 SS payload |
| --- | --- | --- |
| CS-INVITE-01 | `team_send_invitation` | invitee 无效零上行；显式 team 不存在；未指定 team 时复用 current；无 current 时 create→本地注册→add_invitation；权限不足零 add-invitation；payload 含完整 team_key、inviter/invitee、派生私有频道、source，开始/过期时间留给 room |
| CS-INVITE-02 | approve/reject invitation | 无 pending/已过期零上行；正常请求 sender/invitee/version/router/shared_member_data 完整；client_result 透传；成功和 room-not-found 的缓存终态见 ADM-SELF-06 |
| CS-JOIN-01 | `team_send_join_request` | 已在队、有效 pending 时零上行；正常 payload 含 team_key/requester/本人私有频道/source/client_version/router/member_admission_data；channel-not-found 映射 room-not-found；缓存只由 `apply_join_request` 建立 |
| CS-JOIN-02 | accept/reject join request | team 不存在/角色不足零上行；正常 action 含 team_key/requester，业务结果透传 |
| CS-MEMBER-01 | exit/remove/role | self remove 走 manager exit，reason=`EXIT_TEAM`；移除他人需权限且 action reason=`REMOVE_MEMBER`；set-role 权限/role 边界及 payload |
| CS-CAPTAIN-01 | transfer captain | 当前队长可转移，非队长需 OWNER；action 目标完整，最终角色语义由 ROLE-02 验证 |
| CS-DATA-01 | update member data | 空列表、不可写模块零上行；payload 自动填 self user/channel/version/router、key 唯一、permission=MEMBER、ready 更新附加 team-not-matching condition |
| CS-DATA-02 | update team data | 空列表、角色不足、不可写模块、同批重复 key 零上行；matching=true 附加 all-members-ready condition；client_result 透传 |

### 5.6 P2：健壮性

| ID | 场景和关键断言 |
| --- | --- |
| ROBUST-01 | 个人频道错误 Any/无法解包 action 不修改任一缓存；后续更高 sequence 的合法 action 可正常处理 |
| ROBUST-02 | 队伍快照错误 Any 不清空旧缓存、不产生伪 snapshot；后续合法快照可恢复 |
| ROBUST-03 | 队伍 action 中错误 shared-data Any 只跳过该 key，不丢同批合法 key，不创建错误 unpacked 输出 |
| ROBUST-04 | runtime/task 前置失败、未消费 mock、hard timeout、时间 offset 泄漏均使 case 失败；组内连续运行两次无 singleton/dirty handle 污染 |

### 5.7 覆盖映射（2026-08-30）

全部 68 例位于 `src/lobbysvr/test/lobbysvr_test_user_team_*.cpp`，组名 `lobbysvr_user_team`。下表只列
矩阵 ID 与用例名（同文件省略前缀）；§5.5 的 CS 用例名与矩阵 ID 同名（`cs_invite_01_*` 等 8 例，cs 文件）。

| 矩阵 ID | 覆盖用例 |
| --- | --- |
| MEM-01 | manager.`dump_snapshot_exports_cached_state`；robust.`robust_snapshot_rebuild_cleans_pendings_and_indexes` |
| MEM-02 | manager.`incremental_actions_update_cache_and_dirty_push`；robust.`robust_duplicate_events_idempotent`；admission.`team_add_member_clears_joined_user_pendings` |
| MEM-03 | cache.`member_update_full_fields_and_dirty_projection`、`member_update_rejected_without_cache_or_dirty_push` |
| MEM-04 | kick.`kick_channel_action_fallback_without_personal_notify`；robust.`robust_duplicate_events_idempotent` |
| ROLE-01 | manager.`member_set_role_updates_cached_data`；cache.`member_set_role_full_payload_update_cache` |
| ROLE-02 | cache.`election_captain_role_semantics` |
| TEAM-01 | cache.`team_update_shared_data_and_snapshot_admission_filter`；robust.`robust_bad_shared_data_any_skips_only_that_key` |
| DIRTY-01 | manager.`dump_snapshot_exports_cached_state`；lifecycle.`user_get_info_exports_only_running_team_with_trimming` |
| DIRTY-02 | manager.`incremental_actions_update_cache_and_dirty_push`；cache.`member_update_full_fields_and_dirty_projection`、`member_set_role_full_payload_update_cache` |
| KICK-01 | kick.`kick_channel_event_first_then_personal`、`kick_personal_event_first_then_channel` |
| KICK-02 | kick.`kick_channel_action_fallback_without_personal_notify` |
| KICK-03 | kick.`kick_snapshot_fallback_without_personal_notify` |
| KICK-04 | kick.`kick_exit_retry_converges_while_snapshot_still_has_self`、`kick_personal_notify_fallback_until_wal_remove` |
| DEST-01 | kick.`destroy_action_pushes_increase_before_cleanup`、`channel_destroyed_event_synthesizes_destroy_notify`、`personal_destroy_notification_pushes_increase` |
| ADM-TEAM-01..07 | admission.`team_admission_snapshot_load_filters_and_orders`、`team_add_invitation_upsert_and_projection`、`team_invitation_result_removes_pending`、`team_add_join_request_upsert_and_projection`、`team_join_request_result_removes_pending`、`team_add_member_clears_joined_user_pendings`、`team_admission_expiry_boundary_and_cleanup` |
| ADM-SELF-01..10 | admission.`self_invited_notification_full_payload`、`self_join_request_receipt_full_payload`、`self_pending_upsert_reorder_and_segmented_cleanup`、`self_pending_admission_insert_validation`、`self_reject_notifications_remove_pending`、`approve_reject_invitation_result_contract`、`join_team_clears_own_pending_admissions`、`self_pending_second_refresh_and_expired_reapply_precheck`；lifecycle.`table_dump_init_round_trip_restores_watermark_team_and_pendings` 与 manager.`table_roundtrip_restores_pending_admissions`（09）；admission.`self_private_channel_sequence_watermark` 与 manager.`member_events_manage_pending_admissions`（10） |
| JOIN-01..03 | lifecycle.`joined_team_notification_registers_team`、`switch_team_moves_previous_to_pending_exit`、`rejoin_pending_exit_team_swaps_back`；manager.`rejoin_pending_exit_team_restores_current` |
| CREATE-01/02 | lifecycle.`create_team_registers_owner_and_clears_pending`、`create_team_failure_passthrough_and_wal_replay` |
| SEQ-01..03 | lifecycle.`snapshot_saved_sequence_guards_incremental_replay`、`compacted_snapshot_overrides_cache_and_swallows_replay`、`stale_destroy_does_not_remove_new_generation` |
| HB-01/02 | lifecycle.`heartbeat_reports_watermark_and_throttles`、`heartbeat_suppressed_outside_running_member_state` |
| EXIT-01/02 | lifecycle.`exit_team_request_then_channel_remove_converges`、`exit_retry_timeout_cleanup_and_channel_destroy` |
| DUMP-01/02 | lifecycle.`table_dump_init_round_trip_restores_watermark_team_and_pendings`、`user_get_info_exports_only_running_team_with_trimming`；manager.`dump_snapshot_exports_cached_state` |
| CS-INVITE/JOIN/MEMBER/CAPTAIN/DATA | cs.`cs_invite_01/02`、`cs_join_01/02`、`cs_member_01`、`cs_captain_01`、`cs_data_01/02`（共 8 例） |
| ROBUST-01..04 | robust.`robust_bad_any_personal_event_ignored`、`robust_bad_any_team_snapshot_preserves_cache`、`robust_bad_shared_data_any_skips_only_that_key`、`robust_fixture_time_and_runtime_hygiene` |
| §3 叙事补强 | robust.`robust_duplicate_events_idempotent`、`robust_out_of_order_events_converge`、`robust_late_events_after_channel_destroy_ignored`、`robust_snapshot_rebuild_cleans_pendings_and_indexes` |
| 生命周期收编对照 | manager.`minute_refresh_removes_never_member_current_team`、`minute_refresh_keeps_member_current_team` |

## 6. 执行期关闭的实现缺陷（2026-08-30 全部关闭）

执行期按计划先观察 RED 再修复实现，以下条目均已由 §5.7 映射的用例锁定并转绿：

1. `user_team` 的 `election_captain` 默认角色强制 OWNER → 改为显式 role 用事件值、默认继承原队长角色、
   原队长缺失才回退 OWNER（`cache.election_captain_role_semantics`）；
2. manager 的 invitation 插入缺少无效 team_id 校验 → 已补，与 join-request 一致
   （`admission.self_pending_admission_insert_validation`）；
3. `add_team` 不清理同 team_key 的自己的 pending → 注册时连带删除
   （`admission.join_team_clears_own_pending_admissions`、`lifecycle.create_team_registers_owner_and_clears_pending`）；
4. 个人 `remove_member` 丢弃 `remove_member_reason` → 透传真实 reason（kick 三例及 EXIT 系列）；
5. approve/reject invitation 遇 room 不存在时保留失效 pending → `DTMQ_CHANNEL_NOT_FOUND` 映射为确定性
   失败并删除本地 pending（`admission.approve_reject_invitation_result_contract`）；
6. table 只持久化水位不持久化自己的 pending → `svr.local.table.proto` 的 `user_team_data` 增加两类 pending
   字段，dump/init 往返恢复（`lifecycle.table_dump_init_round_trip_restores_watermark_team_and_pendings`）；
7. destroy 路径客户端可能收不到解散通知 → action/personal/频道销毁三条路径都在 manager 移除前下发
   destroy increase 并注销 dirty handle（kick.`destroy_action_*`、`channel_destroyed_*`、`personal_destroy_*`）；
8. test target 缺 task-action 源码 → CMake 已编入，CS 矩阵 8 例落地；
9. 固定 `pump_rounds`/固定 push 数/“默认 30s” → 全面改为 §4.3 可观察条件；`wait_add_member_timeout` 读配置
   （默认 5s），heartbeat 间隔读配置（120s）。

执行期另发现并已修复两个计划外缺陷：

10. `member_update` 的 dirty 投影在 action 缓存副本上原地解包 shared data，污染后续投影与缓存
    → 投影层改为构建剥离后的副本（`cache.member_update_full_fields_and_dirty_projection`）；
11. `do_team_shared_data` 把空 type_url+空 value 的条目误判为新模块类型 → 明确为删除标记语义，与
    `add_member` 的“空数据不动”对齐（`cache.team_update_shared_data_and_snapshot_admission_filter` 等）；
12. 删除标记和快照/销毁的缓存重置都不驱动共享数据处理器，派生状态（`is_matching_`）残留旧值
    → 队伍/成员两张处理器表都新增幂等 `do_delete(ctx, team, [user_key,] key)` 回调（key 标识被删模块）。
    触发口径统一为“只对真实删除的数据项回调”：删除标记分支仅在缓存中确有该 key 时触发（成员侧与
    `do_update` 一样只对自己的数据回调）；`reset_cached_state(ctx)` 只遍历缓存中真实存在的项（队伍侧遍历
    `cached_team_shared_data_`，成员侧只遍历自己的成员条目），未缓存的注册 key 不产生回调。快照重建经
    同一入口（`cache.team_update_*`、`robust.robust_bad_any_team_snapshot_preserves_cache` 断言
    `is_matching()` 终态）。

## 7. 完成证据（2026-08-30）

实施顺序（夹具整理 → P0 缓存 → P0 丢消息 → RED 修复 → P1 生命周期 → CS 矩阵/P2 健壮性）已全部执行完毕。
回归验证命令（`<BUILD_DIR>` 按仓库 build/test Skill 解析，注意 Windows DLL PATH）：

```powershell
cmake --build <BUILD_DIR> --target atf4g-co-lobbysvr-unit-test --parallel <workspace-parallelism>
<BUILD_DIR>\publish\bin\atf4g-co-lobbysvr-unit-test.exe -r "lobbysvr_user_team.<focused_case>"
<BUILD_DIR>\publish\bin\atf4g-co-lobbysvr-unit-test.exe -r lobbysvr_user_team
ctest --test-dir <BUILD_DIR> -R "^atf4g-co-lobbysvr-unit-test\.unit$" --output-on-failure
```

验证记录（Windows 11 amd64、MSVC 19.44、Debug、`build_jobs_cmake_tools`，ninja；§5.7 的 68 例全部落地）：

- 编译：`cmake --build build_jobs_cmake_tools --target atf4g-co-lobbysvr-unit-test --parallel 12` 通过，
  含 12 个 `task_action_team_*.cpp` 在内的全部测试源文件；`svr.local.table.proto` 变更后相关生成代码已重建；
- 组结果：`atf4g-co-lobbysvr-unit-test.exe -r lobbysvr_user_team` → 68 选中 / 68 通过 / 0 失败；
- 受影响 suite：全量运行（含 chat、matching、async-jobs 既有用例）→ 76 选中 / 76 通过 / 0 失败；
- ctest：`ctest -R "^atf4g-co-lobbysvr-unit-test\.unit$"` → 1/1 Passed（4.26s）；
- 跳过项：无；未验证平台：Linux、Release 配置（本仓库 CI 覆盖）。

后续维护入口：改组队行为时先更新 §2/§3 契约，再按 §5.7 找到对应用例调整；新增用例遵循 §4.3 的可观察
同步约定和 id 段分配。上述验证命令照旧可用，`<BUILD_DIR>` 按仓库 build/test Skill 解析。
