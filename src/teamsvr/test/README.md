# Team Room 单元测试

测试入口为 `atf4g-co-teamsvr-room-unit-test`，通过 `project_add_rpc_unit_test` 注册。
测试使用真实 Room、manager 和 action，使用离线 RPC fixture 控制外部服务响应、频道事件和时间。

## 测试范围

本目录验证组队服务拥有的行为：队伍创建、成员和权限、邀请和申请、共享数据、通知、Room 快照恢复及维护任务。
外部频道服务是测试依赖；模拟其成功、失败和回调顺序，用于检查 Team 是否正确处理结果。
DTMQ 的数据库实现、WAL 存储、哈希、检查点、GC、跨节点所有权迁移和投递可靠性不属于 Team 的验收范围。
历史测试文件名中的 `wal` 不表示需要在这里验收 DTMQ 的内部实现。

`service/match/logic/action/task_action_search.cpp` 仍是搜索接口占位实现，不计入已完成的 Room 功能。
多进程部署、滚动升级、压力测试、性能基准和自动缩减随机失败输入也不属于本单元测试入口。

## 当前行为与用例

| 行为 | 测试入口 |
| --- | --- |
| Room 生命周期、创建、销毁、恢复和 manager 回收 | `teamsvr_room_test_lifecycle.cpp`、`teamsvr_room_test_recovery.cpp` |
| 成员角色、操作者身份及操作权限 | `teamsvr_room_test_permission.cpp`、`teamsvr_room_test_action.cpp` |
| 邀请、申请、接纳、取消和刷新 | `teamsvr_room_test_admission.cpp` |
| 频道事件、个人通知内容和实际 flush 行为 | `teamsvr_room_test_event.cpp` |
| 异步任务交错、快照与已提交事件的状态恢复 | `teamsvr_room_test_concurrency.cpp`、`teamsvr_room_test_wal.cpp` |
| 下述失败、边界和历史数据兼容场景 | `teamsvr_room_test_contract.cpp` |

Room 以 `(zone_id, team_id)` 标识，`zone_id == 0` 是合法全局区。
创建请求可以用 `team_id == 0` 申请 UUID；UUID 分配失败时不得创建 Room 或写入频道。
省略队伍类型使用普通队伍配置，显式指定不存在的类型必须拒绝。
节点选择按分区筛选后使用队伍 ID 路由；跨区转发应保留请求内容，并返回远端错误。

共享数据采用 repeated 键值项。同一请求中的重复键必须在写入前拒绝，空 Any 表示删除已有键。
成员和队伍共享数据按键合并；接纳记录刷新使用全量替换，并忽略条目排列顺序的差异。
Room 状态由已提交的频道事件推进；失败响应、响应先到或事件先到均不能凭空改变状态。

快照同时包含公开和私有状态以及创建、压缩信息。
Room 必须拒绝损坏或矛盾的数据，不得在失败恢复过程中写入或发送个人通知。
恢复后只重放保存边界之后的事件，并保留成员心跳恢复保护。
本地心跳由后续维护保存；保存失败后下一次维护可以再次保存。

接纳和离线状态在截止时间到达时过期；其他持有者的锁必须超过超时时间才可接管。
Room 移除后，原实例的挂起维护任务不得发送旧通知、重新设置定时器或恢复为写入者。
个人通知最多提交一次，flush 不负责自动重试投递失败或尚未完成的提交。

`teamsvr_room_contract` 补充以下场景：

- UUID 分配失败、非法队伍类型、无可用分区节点，以及创建请求的重复成员共享数据键。
- 跨区接纳转发的完整请求、最大值身份字段、远端业务错误、传输失败及迟到成功响应。
- 写入失败、提交事件与响应的两种顺序、响应丢失后的幂等重试。
- 损坏的公开/私有快照、非法保存/压缩位置，以及发布修复数据后的恢复。
- 心跳快照保存失败、销毁失败重试和锁冲突后的停止行为。
- manager 移除旧 Room 后，原维护任务结束时不再发送个人通知。
- 历史 map 编码解析、快照恢复和条件比较。
- 超出实际 RPC 缓冲区的创建请求拒绝、容量内大请求重试，以及恢复后的全部键值内容。
- 三个固定种子的独立键值模型，共 480 次操作，期间定期保存、重建 Room 并继续比较状态。
- 个人通知延迟完成时，重复 flush 不得重复提交。
- 接纳、锁、成员离线、空房销毁的截止时间前后边界。
- 最大计数、比例和时长配置下不提前清理、不溢出，并安排将来的维护时间。

## 历史协议数据

`teamsvr_room_legacy_wire.h` 固定保存提交
`f45daa3b26840861ca1028f1de46027dff0c5141` 的旧 map 协议生成的二进制数据。
数据覆盖成员、队伍、邀请、申请、私有状态和条件，不使用当前 repeated 协议自行编码后再解码。
修改当前协议时保留这些字节，以便继续检查既有数据能否读取。

## 构建与执行

复用工作区已配置的生成器、工具链和并行度。在已初始化 MSVC 环境的 PowerShell 7 中执行：

```powershell
cmake --build build_jobs_cmake_tools --target atf4g-co-teamsvr-room-unit-test atf4g-co-lobbysvr-unit-test --parallel 7
ctest --test-dir build_jobs_cmake_tools -V -R '^atf4g-co-(teamsvr-room|lobbysvr)-unit-test\.unit$' --output-on-failure --no-tests=error
```

其他工作区将 `build_jobs_cmake_tools` 替换为其实际构建目录，并使用该目录的并行度设置。
CTest 提供离线资源工作目录和运行环境；验收以可执行程序报告的实际用例数与退出码为准。

2026-09-15 在 Windows、MSVC Debug 配置下运行：Team Room 174/174，相关 Lobby 回归 104/104。
本次新增 17 个 Room 用例；这些结果不代表其他平台或多进程环境已验证。
