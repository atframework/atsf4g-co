# dtmq 组件 Code Review 报告 (kimi-code / kimi-k3)

- **审查工具/模型**: Kimi Code CLI (model: kimi-k3)
- **审查日期**: 2026-07-18（第五轮，第13轮修订后复审）
- **审查范围**: `src/component/dtmq` 下全部非生成代码（排除 `*.atfw.gen.h` / `*.atfw.gen.cpp`；CMake 文件按约定不审查；不做函数复杂度分析）
- **审查基线**: git HEAD `c33b0598`（第12轮修订）+ 工作区未提交改动（`mq_channel.cpp`、`mq_channel_manager.{h,cpp}`，下称"第13轮改动"）
- **审查方式**: 全量静态审查（逐 diff 复核 + 所有结论人工核对到框架/库源码）；未运行构建、clang-tidy、cpplint
- **上一轮报告**: 基线 `c33b0598`（第12轮修订）

## 结论摘要

第13轮改动针对上一轮的 M1/M2 做了修复，并引入 `reactive_io_channels_` 机制重构 pending IO 队列。复核结论：**M2 已有效修复**（`upgrade_to_writable` 对 readonly→writable 提升补脏标记，覆盖了"快照先于 DB 加载到达"的主要场景）；**M1 的修复方向正确但关键条件写反了**——`update_timer` 的新 GC 条件把"销毁未保存"的频道送入 GC 分支（不保存、到期清理），把"销毁已保存"的频道留在保存路径，与注释意图恰好相反，M1 的回滚场景依然存在。新增的 reactive 机制本身经逐项核查无明显缺陷。本轮无其他高严重度发现。

---

## 本轮新发现

### 🟡 M1（持续，修复写反）. `update_timer` 的新 GC 条件把"销毁未保存"与"销毁已保存"两类频道放反了分支

- **位置**: `dtmq-proxysvr/data/mq_channel.cpp:2311-2312`
- **现象**:

  ```cpp
  // 如果时第一次destroy保存失败，这里也要保存成功后才能清理   ← 注释意图：未保存的销毁必须先保存
  if ((!is_writable() && !is_readonly()) ||
      (!is_available() && (!is_writable() || destroy_sequence_ > saved_sequence_))) {
    // GC 分支（cache_expire 后移除，不做任何保存）
  }
  // 否则：正常路径（周期保存 + 无订阅者 GC）
  ```

  逐项真值核对（"销毁已保存" ⟺ `saved_sequence_ >= destroy_sequence_`）：

  | 场景 | 第二子句求值 | 落点 | 是否符合意图 |
  | --- | --- | --- | --- |
  | 销毁的 readonly 副本 | `!is_available() && !is_writable()` → true | GC 分支 | ✅（副本可重新拉取） |
  | 销毁的 writable，**已保存** | `!is_available() && (false \|\| false)` → false | 正常路径（周期保存+无订阅者 GC） | △ 可接受但每次 interval 冗余保存 |
  | 销毁的 writable，**未保存**（保存失败） | `!is_available() && (false \|\| true)` → **true** | **GC 分支** | ❌ 与注释意图相反 |

  即注释要保护的"destroy 保存失败"频道恰好落入不保存、到期即 GC 的分支：进程内销毁状态丢失后，writable 从 DB 重新加载为销毁前的 available 记录，副本经快照归一化也被重置，**销毁状态集群级回滚（上一轮 M1 的场景依然存在）**。
- **建议**: 第二子句中的 `destroy_sequence_ > saved_sequence_` 改为 `destroy_sequence_ <= saved_sequence_`（即 GC 条件 = kNone、readonly 不可用、或销毁已落盘的 writable）；更稳妥的写法是直接复用 `!need_save_db()`：`!is_available() && (!is_writable() || !need_save_db())`。
- **附带说明**: 本轮新增的 `remove_running_io_channel` reactive 重新入队（`mq_channel_manager.cpp:711-724`，条件 `is_writable() && (need_save_db() || is_stoping_)`）在非停机场景下会把销毁未保存的频道重新推入 pending，但 `resolve_channel_distribution` 的非停机分支只走 `async_start_transfer`（目标为 0 时 no-op，`mq_channel.cpp:1303-1305`），随后即被 erase——该回路救不了保存重试，M1 的修复必须落在上述 GC 条件上。

### 🟢 L1（新增）. `update_timer` 的 GC 分支未检查 `is_io_task_running()`

- **位置**: `mq_channel.cpp:2311-2328`
- **现象**: 与无订阅者 GC 分支（`:2368-2370` 有 `is_io_task_running()` 保护）不同，`:2311` 的 GC 分支不检查 IO 任务。destroy 流程的保存任务在途时若恰好跨过 `cache_expire_interval`，频道可能在任务进行中被 `remove_channel`（任务的 `self_ptr` 保证对象不悬垂，但频道被移出管理表后，完成回调里的 `get_channel` 判等失败、reactive 重新入队被跳过）。
- **影响**: 窄窗口内的管理状态短暂不一致（任务本身安全）；本轮新条件把"销毁未保存"频道也纳入该分支后暴露面略有扩大。
- **建议**: GC 分支补 `is_io_task_running()` 检查，运行中则重排定时器。

### 🟢 L2（新增）. 销毁已保存的 writable 频道每次 `save_interval` 冗余落盘

- **位置**: `mq_channel.cpp:2331-2345`（周期保存无 `is_dirty()` 检查）配合 `:2311-2312` 新条件
- **现象**: 销毁已保存的 writable 频道落入正常路径后，周期保存不看 `is_dirty()`，每个 `save_interval` 都会把同一份销毁状态重写一次 DB，直到无订阅者 GC 生效。无正确性问题，纯写放大。
- **建议**: 周期保存前补 `is_dirty()` 检查（顺带可避免健康频道的空转保存）。

---

## 第13轮改动逐项复核

### ✅ M2（上轮）已有效修复

`upgrade_to_writable`（`mq_channel.cpp:2443-2453`）对 readonly→writable 提升补 `++dirty_version_`：readonly 副本经 `load_snapshot` 标记的 saved 状态在提升时被重新置脏，周期保存/停机兜底随后正常落盘，覆盖了上一轮 M2 的主要场景（transfer 快照先于 DB 加载到达）。`load(ctx, record)` 守卫跳过路径仍会执行 `saved_version_` 同步（`:229-231`），但紧随其后 `:235-236` 的 `upgrade_to_writable` 会重新置脏，净效果正确。kNone→writable 的初次 DB 加载不补脏（刚落盘的数据本来就干净），处理精确。关闭此项。

### ✅ `reactive_io_channels_` 机制复核（无缺陷，一处附带说明）

- 机制：`resolve_channel_distribution` 把"任务在途"的条目从 pending 移入 reactive 集合（`mq_channel_manager.cpp:805-810,822-826`）；`remove_running_io_channel` 在任务完成时按 `is_writable() && (need_save_db() || is_stoping_)` 决定是否重新入队（`:711-724`）；`stop()`/`add_channel` 推入 pending 前先从 reactive 擦除（`:214-216,:275-278`）。
- 已核实：reactive 集合只存裸指针，但条目仅在 IO 任务在途时存在（任务持有 `self_ptr`），完成回调与析构（`~mq_channel:112-114`）都会走 `remove_running_io_channel` 擦除，无悬垂解引用路径；`get_channel` 判等（`channel_ptr.get() == channel`）正确防实例替换；discovery 重推造成的 pending/reactive 双挂由 `compact_pending_io_channels` 去重；停机时 reactive 条目被 `stop()` 清回 pending，完成回调按 `is_stoping_` 重新入队，`is_can_stopped` 闭环成立。
- 附带说明（非缺陷）：只读频道任务完成后不再重新入队（原行为是留在 pending 下轮重试），与"只读转移失败可懒恢复"的设计一致，属可接受的行为变化。

---

## 既往发现状态（截至本轮）

### ❌ 仍存在

- **M3** `make_readable_channel_with_replicate_index`：残留 kNone 频道 + 本机为分布目标时误报 `EN_ERR_MAINTENANCE`（`mq_channel_manager.cpp:523-565`），与 `make_readable_channel`/`make_writable_channel` 同场景行为不一致；`get_target_distribution_server_id` 无越界归一化。
- **L1（上轮）** `set_created`/`set_destroyed` 的 FWLOGINFO 打印传入参数而非日志实际分配值（`mq_channel.cpp:1865-1867`、`:1910-1912`）；`task_action_destroy_channel:87` 每次 destroy 白烧一个 sequence（建议直接传 0）。
- **L2（上轮）** pull（`task_action_pull.cpp:97`，`seq < compact` 跳过、保留边界）与 dump（`mq_channel.cpp:364`，`<=` 跳过、丢弃边界）对 compact 边界消息处理仍不一致。
- **L3（上轮）** proto 删除 `stateful_hash_code` 字段 9 未加 `reserved`。
- **L2（原）** 日志文案复制粘贴："transfer: create task failed"：`mq_channel.cpp:573`（writable_init）、`:650`（readonly_init）、`:1363`（async_save）。
- **L11** `create_mq_channel_publisher`/`create_mq_channel_client` 的 `private_data` 死初始化（`mq_channel_wal_handle.cpp:713-726`）。
- **L12** `publisher_send_logs`（`:505-509`）与 `publisher_send_snapshot`（`:389-397`）错误分类不对称。
- **L13** `on_log_added` lambda 返回值无语义（`mq_channel_wal_handle.cpp:217-230`）。
- **L15** libstdc++ ABI 注释与分支归属仍相反（`mq_channel_manager.cpp:176-184` vs `:744-751`）。
- **L16** `send_oss`/`report_channel_qty_oss` 空实现 FIXME（`mq_channel.cpp:2440+`、`mq_channel_manager.cpp:832-841`）。
- **L18** `task_action_destroy_channel:54-58` 仍吞 `EN_ERR_DTMQ_INVALID_CHANNEL`。

### ✅ 累计已修复（第9轮基线以来）

- 第10轮：H1（notify 处理器注册）、H2（IO 并发计数为 0 挂起）、H3（assign 回调悬垂 UAF）、H4（tick 内 wal_client UAF）、H5（assign_logs 哈希重锚）、L6/L8/L10；
- 第11轮：N1（DB 加载空消息回归）、N2（库右值重载重算）、L3/L4/L5/L17/L19/L20/L22/L24/L25 等；
- 第12轮：N1（writable create/destroy 成员赋值，delegate skip 删除）、N2（pull 过滤条件）、N3（destroy 签名残留）、N4（load 覆盖式赋值）、N6（wal_client_ 死分支）、L14（停机保存 need_retry）；
- 第13轮：M2（提升 writable 补脏标记）。

### ⏹ 已过时（重设计取代）

- 早期 M1/M2（not_found 本地模拟销毁链）：已被"create/destroy 与普通消息一样经 WAL 同步"的重设计整体取代。

---

## 已核实无问题项（含项目方 Hint 逐条核对）

1. **公共 `send_message` 可发送控制命令**：设计如此（功能扩展）。**符合设计**。
2. **`task_action_subscribe` 订阅转发失败仅记日志**：后续有心跳超时重试恢复。**符合设计**。
3. **快照等价判断只比较 WAL 尾部**：`mq_channel.cpp:703-730` 仅比较 `last_sequence`+`last_hash_code`。**符合设计**。
4. **`EN_ERR_*` 错误码均来自 `com.const.pb.h`**：`EN_SYS_*` 在 `err::` 子命名空间，无头文件包含问题。
5. **CMake 未审查、未做函数复杂度分析**。
6. create/destroy 全流程主路径（创建→写入→销毁→重建→副本/Client 同步→DB 恢复）逻辑自洽（第12轮修复后成立，本轮 M1 仅涉及"销毁保存失败"的容错缺口）。
7. `need_save_db()` 表达式（`mq_channel.cpp:1240-1243`）：占位不保存、已保存销毁不重复、未保存销毁必保存，语义正确（M1 是 GC 分支条件未复用该语义）。
8. reactive 机制生命周期与去重（见前文专项复核）。
9. 工程规范：版权头、protobuf prefix/suffix 包裹、导出宏、include 排序均符合仓库约定。

## 验证与跳过项

- 本次为**纯静态审查**：未运行构建、clang-tidy、cpplint、单元测试（审查任务未改动代码）。
- M1 的真值表经逐场景代入验证（readonly/writable × 已保存/未保存）；reactive 机制的生命周期安全性经全部移除/完成回调路径排查。
- 建议补测场景："destroy 保存失败后频道 GC/重启，销毁不回滚"（修 M1 后验证）。

## 建议处理优先级

1. **必须修复**: M1（`>` 改 `<=`，一行；否则第13轮的容错目标未达成）。
2. **建议修复**: L1（GC 分支补 IO 任务检查）。
3. **择机清理**: L2（写放大）、M3 及残余低级项。
