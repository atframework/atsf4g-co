# Lobby 组队逻辑与单元测试

本目录负责把队伍服务的状态维护为玩家本地缓存，校验组队 CS 请求，并向客户端提供组队数据。

## 模块边界

| 代码 | 本目录负责的行为 |
| --- | --- |
| `user_team_manager` | 当前队伍和退出队列、两类个人 pending、个人事件序号、table dump/init、RPC 打包和结果处理、组队 dirty 的统一登记与导出 |
| `user_team` | 成员/队长/配置/共享数据/队伍级 admission 缓存、快照与事件消费、心跳和退出重试、客户端视图、对象替换后的回调隔离 |
| `user_team_algorithm` | 共享数据 key 编码和客户端可写模块判断 |
| `user_team_battle_library_function` | 默认共享数据、规范化处理、匹配/关卡回调的队长和成员门槛、缓存只读接口及本地数据修复 |
| `task_action_team_*` | 12 个组队 CS 请求的参数、登录和权限检查，以及 manager/user_team 调用结果 |

Room、DTMQ、matching、chat、user 和 RPC 框架是外部依赖。
测试可以使用这些依赖的真实入口驱动本目录代码，但验收对象是本目录的返回值、缓存、上行请求和客户端组队通知。
Room 的通知生成和权限裁剪、DTMQ 的 WAL/数据库/故障转移、匹配算法与匹配流程结果、聊天发送机制、通用测试框架自测不属于本目录的测试要求。
现有跨模块回归继续保留，不以增加其他模块的覆盖作为本目录完成条件。

## 当前行为

### 队伍和恢复

同一队伍类型只有一个 current。切队时旧队伍进入 pending-to-exit，重试退出至确认离队、频道销毁或超时。
退出中的队伍不作为当前队伍导出；同队重新加入可以恢复退出队列中的对象。
队伍身份使用完整 `(zone_id, team_id)`。旧入队数据未携带 team_type 时按普通队伍恢复，显式未知类型不恢复。
创建成功通过响应直接注册创建者，无需等待 joined_team 通知；创建失败不修改输出 key 和本地索引。

成员、队长、配置和共享数据由频道快照及事件更新，RPC 成功不提前修改这些缓存。
完整快照重建缓存，并覆盖重放期间的增量；metadata 更新不触发缓存重建。
旧对象被移除后，其迟到事件、快照和销毁回调不能影响当前对象。
Room 明确返回频道不存在、队伍销毁或操作者不在队伍中时才修复本地成员状态；普通权限或传输错误不证明玩家已经离队。

### 邀请和申请

`user_team` 保存本队候选人的邀请/申请，以用户 key 索引。
manager 保存当前玩家收到的邀请及发出的申请，以队伍 key 索引。
两层均维护按过期时间排列的列表，同 key 更新保持索引一致，到期即失效。
个人 pending 的删除登记对应 dirty remove，留待正常 flush 下发。
manager 的 table 数据保存当前队伍、个人 pending 和已处理的个人频道序号；退出队列不落地。

### 客户端数据

首次拉取 `need_user_team` 前不推送组队 dirty。
get-info 显式返回完整 `DTeamUserData`，即使为空也设置字段，并清理已经被该响应覆盖的 dirty。
普通 action 产生增量；首次发布、全量恢复和重新入队产生快照；退出或销毁产生一次 team_remove。
空 action、metadata、重复通知和重复清理不得产生额外快照或空推送。
检查通知时同时检查原始包数和解码后的业务内容，不能仅比较最终缓存。

客户端视图剔除成员/邀请/申请的内部频道、路由及确认信息。
快照中的成员和队伍共享数据解包为对应模块；增量中的成员共享数据解包后随 action 下发，删除保留 key 标记。
个人频道事件和到期清理只登记变化，由已有 CS 收尾、频道回调或用户消息 flush 统一发送。

### 共享数据与 glue

共享数据 key 的高 32 位是 module oneof 字段号，低 32 位是 data_type oneof 字段号。
bool 的 false 值仍保留 oneof 类型及其 key。
客户端只能修改成员的 battle.ready；队伍共享数据没有客户端可写模块。
默认队伍数据包含 matching、matching_team_view 和 matching_start_data，默认成员数据包含 ready 和 matching_parameter。

内部批量更新先按 key 拒绝重复项，再完成规范化，最后统一打包。
matching 更新附带 matching_team_view，ready 更新附带 matching_parameter。
关闭状态时清空旧派生数据，派生条目排在状态字段之前或之后都必须得到相同结果。
ready 更新附加队伍未匹配条件；开始 matching 附加全员 ready 条件。
glue 直接发送的共享数据仍保留自身入口的权限和条件约定，不能与批量更新混用断言。

## 用例入口

测试位于 `src/lobbysvr/test/`，使用既有 `project_add_rpc_unit_test` 目标，组名为 `lobbysvr_user_team`。

| 文件后缀（前缀为 `lobbysvr_test_user_team_`） | 覆盖 |
| --- | --- |
| `manager.cpp`、`lifecycle.cpp` | 注册、切队、退出、心跳、table 往返、完整拉取和快照/增量消费 |
| `cache.cpp`、`admission.cpp` | 成员/队长/共享数据、两层邀请申请、更新/删除/到期及数据裁剪 |
| `kick.cpp`、`dirty.cpp` | 个人通知缺失后的本地恢复、移除去重、首拉门槛、通知内容和实际下发边界 |
| `cs.cpp` | 组队 CS 参数、权限、完整上行 payload、业务错误及本目录关卡回调 |
| `matching_sync.cpp` | 本目录匹配 glue 的门槛、上行、频道回调、定期修复和缓存读取 |
| `robust.cpp` | 非法输入、重复/乱序/迟到事件和缓存恢复 |
| `contract.cpp` | 独立 key 编码检查，已有派生条目的清空、输入顺序、上行和客户端通知 |

## 构建与验证

复用工作区构建目录、生成器、配置和并行度。在已初始化 MSVC 环境的 PowerShell 7 中执行：

```powershell
cmake --build build_jobs_cmake_tools --target atf4g-co-lobbysvr-unit-test --parallel 7
ctest --test-dir build_jobs_cmake_tools -V -R '^atf4g-co-lobbysvr-unit-test\.unit$' --output-on-failure --no-tests=error
```

直接运行可执行程序时，先复用 CTest 的工作目录、`RPC_UNIT_TEST_WORKDIR` 和 DLL PATH，再使用
`-r lobbysvr_user_team` 或 `-r lobbysvr_user_team.<case>`。
以实际执行的用例数、失败数、跳过项和退出码为准；发现用例或运行空组不算通过。

## 验收记录（2026-09-15）

在 Windows/MSVC Debug、Ninja、`build_jobs_cmake_tools` 中重建并通过实际执行：

| 范围 | 通过数 |
| --- | --- |
| `lobbysvr_user_team` | 99/99 |
| Lobby 全量（包含组队） | 114/114 |
| Team Room 回归 | 174/174 |

本轮新增 `contract.cpp` 的 3 个用例。两个规范化用例先复现旧实现失败，再验证修复后的两种输入顺序、上行内容、缓存和客户端通知。
Team Room 作为受影响交互的回归检查，其内部实现不计入本目录的覆盖要求。

C++ 格式与 cpplint、CMake 格式与 lint、`git diff --check` 通过。
clang-tidy 对新增用例和本轮修改代码无诊断；整文件检查另报告 `user_team.cpp` 四个既有只读接口的
`bugprone-exception-escape`，它们涉及 protobuf 分配与 noexcept，本轮未修改。
clang-tidy 使用当前编译数据库的临时副本，移除 MSVC PCH 参数。本轮未运行 `clang-analyzer-*`，其他检查使用 `.clang-tidy` 配置。
未安装 markdownlint，Markdown 已检查标题、链接和 CRLF；Linux 和 Release 未执行。
