# 等待室 robot 验收

这些用例调用真实 CS 协议，经过 Lobby、Match 和 Orbit。`matching_wait_admitted` 同时要求匹配状态
`FINISHED`、成功结果、预期关卡和有效 faction，以及 `user_get_info` 返回非空 room key、地址、token 和
`init=true`。这证明匹配和 Orbit 初始化成功，不要求 robot 真实连接 DS。

每次使用全新账号前缀，重复次数为 1。替换前缀时也要替换行末引用的完整 OpenID；同一组账号、等待室测试池
不能由多个 robot 并行使用。执行前须生成资源、重新生成 robot 协议并构建，确认服务和实际 DS 可以正常启动。

`matching_level_select <level_id> [region] [fill_policy] [level_ids_csv]` 的参数保持不变。
选关请求现在直接发送 `DLevelSelect`，包含地区、关卡集合和补人策略，不再发送战斗版本。
用例选关成功后通过 `user_get_info(need_user_level_select=true)` 拉取保存结果，核对关卡集合、地区和补人策略；
字段缺失或内容不一致会立即失败。关卡集合不要求顺序一致。`matching_start` 仍无参数，使用此前保存的选择。

## 配置与覆盖

测试关卡位于 `Level.xlsx`，池和规则位于 `Matching.xlsx`。四池普通成局条件均为 2 人、2 个 faction；
当前人数上限 4、历史成功加入人数上限 8、faction 容量 2、最多 4 个 faction，普通和等待室规则不限制 faction 容量关系。
初始两个单人禁止填充，分别建立容量 1 的完整 faction。
四个关卡使用已有的 DedicatedServer 模板 `1002`（map2，`remote_start=false`），由 Orbit 启动 DS。
模板 `1` 需要客户端处理远程启动通知，当前 robot 不提供该能力，不能用于本组用例。

| 用例 | 关卡 / 池 | 等待室设置 | 核心断言 |
| --- | --- | --- | --- |
| `matching_waiting_fill.conf` | 999904 / 999990401 | 开启，允许补已有 faction | 后补 Unit 新建容量 2 faction 后独立入场；下一 Unit 补相同 faction；当前 4 人占满时，新用户进入另一 DS，原等待室仍保留 |
| `matching_waiting_no_fill.conf` | 999905 / 999990402 | 开启，禁止补已有 faction | 两个后补 Unit 分别新建 faction，进入同一 DS；原用户 faction 和 Unit ID 不变 |
| `matching_waiting_disabled.conf` | 999906 / 999990403 | 关闭 | 第一对用户完成后，下一单人继续搜索，后续一对进入不同 DS |
| `matching_waiting_end_join_setup.conf` + `matching_waiting_end_join_verify.conf` | 999907 / 999990404 | 开启，2 个初始用户 + 1 个后补用户 | 真实 EndJoin/结算后，新用户进入另一 DS；不依赖加入截止 |
| `matching_waiting_quit_setup.conf` + `matching_waiting_quit_verify.conf` | 999904 / 999990401 | 开启，允许回收名额 | 初始玩家真实退出后，新玩家进入记录中的同一房间并补入相同新 faction |

`matching_assert_admission` 对当前用户和参考用户都重新执行 `matching_check` 和 `user_get_info`，与首次入场保存的
Unit ID、room key、faction 和关卡比较，再检查两者的房间、faction 关系。因此不是仅比较客户端缓存或仅看 FINISHED。
`matching_assert_searching` 要求当前状态为 SEARCHING 且没有 Orbit 房间。

满员场景验证外部可见的“不再接纳、后续用户进入另一 DS”。CS 不暴露 Room 状态，不能仅凭这些断言区分
“Room 已关闭”和“仍在 WAITING 但因容量拒绝”；内部状态由 Match 单元测试和日志验证。退出后能否补位由名额回收配置决定。

## 执行

从发布目录 `robot/bin` 启动，显式指定同时包含匹配表和关卡表的 `Both` 资源目录：

```powershell
Set-Location 'D:/DB/server/build/publish/robot/bin'
.\standalone.ps1 -resource '../../resource/excel/Both'
```

默认配置的 `../../resource/excel` 不直接包含所需匹配表。本地已验证的启动路径需要上面的参数。
进入交互命令后，每次选择一组执行：

```text
run-case-file "D:/DB/server/src/robot/case_config/matching_waiting_fill.conf" 1
run-case-file "D:/DB/server/src/robot/case_config/matching_waiting_no_fill.conf" 1
run-case-file "D:/DB/server/src/robot/case_config/matching_waiting_disabled.conf" 1
```

上一个同池测试的房间必须已关闭接纳，才开始下一个同池测试。登出不会关闭 DS，也不是退出 DS 的证明。
本地 Orbit 容量配置为 `cpu=4`。测试完成后 DS 仍可能驻留并占用容量，所有组连续运行会耗尽可用容量。
逐组执行，在该组完成并保存证据后，由外部 DS 管理入口清理这组已完成测试的 DS，确认有可用容量再继续；
不要在用例中间或截止两阶段之间清理目标房间。
保留 robot 每行 `All Success` / 失败统计和入场断言的 Unit、room、faction 日志；仅进程退出码不能代替验收结果。

单个完整用例也可以非交互执行，完成后 robot 自动退出：

```powershell
.\standalone.ps1 -resource '../../resource/excel/Both' -case_file 'D:/DB/server/src/robot/case_config/matching_waiting_fill.conf' -case_file_repeated 1
```

### 全新账号前缀

现有文件使用固定前缀：`fill` 为 `wrfill`、`no_fill` 为 `wrnofill`、`disabled` 为 `wrdisabled`，
EndJoin 两个阶段均为 `wrendjoin`，退出两个阶段均为 `wrquit`。重复执行前，把所选文件的这一前缀精确替换为新值；相同文本也出现在参考用户
完整 OpenID 中，须一起替换。同一通知用例的两个文件须使用同一个新前缀。

也可以只在 `<BUILD_DIR>/_agent_tmp/` 创建变量模板，利用 robot 现有的 `-set` 替换功能，不改仓库用例：

```powershell
$caseSource = 'D:/DB/server/src/robot/case_config/matching_waiting_fill.conf'
$caseCopy = 'D:/DB/server/build/_agent_tmp/robot-waiting-20261009/matching_waiting_fill.fresh.conf'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $caseCopy) | Out-Null
$caseContent = Get-Content -LiteralPath $caseSource -Raw
$caseContent.Replace('wrfill', '${WAITING_PREFIX}') | Set-Content -LiteralPath $caseCopy -Encoding utf8NoBOM
$runPrefix = 'wrfill' + (Get-Date -Format 'yyyyMMddHHmmss')
.\standalone.ps1 -resource '../../resource/excel/Both' -case_file $caseCopy -case_file_repeated 1 -set "WAITING_PREFIX=$runPrefix"
```

这个示例在 `robot/bin` 运行。固定前缀文件没有 `${WAITING_PREFIX}`，仅传 `-set` 不会改变其账号。

### EndJoin 与玩家退出

等待室不再有 EndJoinTime，也不再因当前满员关闭。两个通知场景都必须在同一个 Robot 进程中执行 setup 和 verify，不能用两次独立的非交互进程代替。

- EndJoin：先执行 `matching_waiting_end_join_setup.conf`，保持账号在线，通过实际入口发送该房间的 EndJoin/结算通知，确认完成后执行 `matching_waiting_end_join_verify.conf`。初始房间留有空位，避免把容量不足误判为成功关闭。
- 退出：执行 `matching_waiting_quit_setup.conf` 后，通过真实 DS/Orbit 流程让 `wrquit1` 退出，再执行 `matching_waiting_quit_verify.conf`。verify 会先等待 Orbit 退出状态，然后验证后补玩家进入原房间。
- `matching_assert_recorded_room <reference_open_id> <same|different>` 读取当前玩家的实时匹配与 Orbit 状态，并和参考玩家此前已验证的房间标识比较；参考玩家可以已退出或已结算。普通 `matching_assert_admission` 仍要求双方实时状态保持不变。

不使用 `logout` 或 `matching_cancel` 冒充 Orbit 退出/关闭通知。每组执行完须通过真实 EndJoin/结算关闭房间，换新账号前缀本身不能清除仍有容量的旧等待室。

历史上限的集成验收可在同一进程分批加入新玩家并触发真实退出，直到历史成功人数达到 8。退出不返还历史额度；后续新 Unit 应进入另一房间。历史额度、并发预留和退出回收关闭分支已有 Match RPC/Room 单元测试覆盖，真实服务验证仍需实际通知入口。

### 退出后重匹配

当前公开 CS/GM 协议没有 DS 主动退出接口。需由真实 DS 的控制入口触发该玩家退出；不能用 `logout`、
`matching_cancel`、修改数据库或伪造内部 `user_finish` 代替。已有动作可在同一 robot 进程按以下顺序接续：

```text
matching_wait_orbit_left true <prefix> <begin> <end> 0 1 1 120
matching_start true <prefix> <begin> <end> 0 1 1
matching_assert_new_unit true <prefix> <begin> <end> 0 1 1
matching_wait_admitted true <prefix> <begin> <end> 0 1 1 150 <level_id>
```

`matching_assert_new_unit` 应在新的 `matching_wait_admitted` 覆盖入场记录之前执行。需要配合其他用户达到普通成局条件。
没有触发和观察真实 DS 退出时，不能声明已覆盖退出后重匹配。

## 本地检查

从 `src/robot` 执行 `go test ./case ./rpc ./task` 和 `go vet ./case ./rpc ./task`。这些检查保护断言和用例配置，
不能代替上述真实服务验收。Go 协议由工程生成；编译显示字段缺失时，先确认生成产物与当前 proto 一致。
