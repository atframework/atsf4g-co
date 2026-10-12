# 等待室 robot 验收

这些用例调用真实 CS 协议，经过 Lobby、Match 和 Orbit。`matching_wait_admitted` 同时要求匹配状态
`FINISHED`、成功结果、预期关卡和有效 faction，以及 `user_get_info` 返回非空 room key、地址、token 和
`init=true`。这证明匹配和 Orbit 初始化成功，不要求 robot 真实连接 DS。

每次使用全新账号前缀，重复次数为 1。替换前缀时也要替换行末引用的完整 OpenID；同一组账号、等待室测试池
不能由多个 robot 并行使用。执行前须生成资源、重新生成 robot 协议并构建，确认服务和实际 DS 可以正常启动。

## 配置与覆盖

测试关卡位于 `Level.xlsx`，池和规则位于 `Matching.xlsx`。四池普通成局条件均为 2 人、2 个 faction；
总人数上限 4、faction 容量 2、最多 4 个 faction，普通和等待室规则不限制 faction 容量关系。
初始两个单人禁止填充，分别建立容量 1 的完整 faction。
四个关卡使用已有的 DedicatedServer 模板 `1002`（map2，`remote_start=false`），由 Orbit 启动 DS。
模板 `1` 需要客户端处理远程启动通知，当前 robot 不提供该能力，不能用于本组用例。

| 用例 | 关卡 / 池 | 等待室设置 | 核心断言 |
| --- | --- | --- | --- |
| `matching_waiting_fill.conf` | 999904 / 999990401 | 开启，允许补已有 faction | 后补 Unit 新建容量 2 faction 后独立入场；下一 Unit 补相同 faction；4 人成功后，新用户进入另一 DS |
| `matching_waiting_no_fill.conf` | 999905 / 999990402 | 开启，禁止补已有 faction | 两个后补 Unit 分别新建 faction，进入同一 DS；原用户 faction 和 Unit ID 不变 |
| `matching_waiting_disabled.conf` | 999906 / 999990403 | 关闭 | 第一对用户完成后，下一单人继续搜索，后续一对进入不同 DS |
| `matching_waiting_deadline_setup.conf` + `matching_waiting_deadline_verify.conf` | 999907 / 999990404 | 开启，2 个初始用户 + 1 个后补用户 | 先证明该房接纳后补，再使用实际绝对截止时间；未满房间到期后，新用户进入不同 DS |

`matching_assert_admission` 对当前用户和参考用户都重新执行 `matching_check` 和 `user_get_info`，与首次入场保存的
Unit ID、room key、faction 和关卡比较，再检查两者的房间、faction 关系。因此不是仅比较客户端缓存或仅看 FINISHED。
`matching_assert_searching` 要求当前状态为 SEARCHING 且没有 Orbit 房间。

满员场景验证外部可见的“不再接纳、后续用户进入另一 DS”。CS 不暴露 Room 状态，不能仅凭这些断言区分
“Room 已关闭”和“仍在 WAITING 但因容量拒绝”；内部关闭状态应结合 Match 日志检查。退出后不重开还需真实 DS 退出动作。

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
截止两个阶段均为 `wrdeadline`。重复执行前，把所选文件的这一前缀精确替换为新值；相同文本也出现在参考用户
完整 OpenID 中，须一起替换。截止两个文件须使用同一个新前缀。

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

### 绝对截止

1. 在同一 robot 进程运行 `matching_waiting_deadline_setup.conf`，保持账号在线。
2. 按该轮输出的 room key，在真实 Orbit → Match `SSMatchingOrbitRoomReadyReq` 日志中找到 `end_join_timepoint`。
3. 将 `matching_waiting_deadline_verify.conf` 的 `__END_JOIN_TIMEPOINT__` 替换成上述 Unix 秒，副本存到
   `<BUILD_DIR>/_agent_tmp/`，然后在同一 robot 进程执行副本。

不要分别用两次非交互 `-case_file` 启动这两个阶段：第一次结束会退出 robot，第二次没有已登录用户和入场记录。

CS 没有提供 `end_join_timepoint`。不要从房间创建时间加一个猜测的等待时长，也不要用等待规则时间上限代替该截止。
`matching_wait_until` 仅安排后续操作的时间，不证明任何匹配行为；其后的 SEARCHING、另一 DS 和原用户不变断言才是验收。
如果 DS 在截止之前主动结束或房间数据清除，此场景前置条件不成立，应查明 DS 行为，而不是删除“原用户不变”断言。

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
