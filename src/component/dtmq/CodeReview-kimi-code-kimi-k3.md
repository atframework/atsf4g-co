# dtmq 组件 Code Review 报告 (kimi-code / kimi-k3)

- **审查工具/模型**: Kimi Code CLI (model: kimi-k3)
- **审查日期**: 2026-07-18（第三轮，第11轮修订后复审）
- **审查范围**: `src/component/dtmq` 下全部非生成代码（排除 `*.atfw.gen.h` / `*.atfw.gen.cpp`；CMake 文件按约定不审查；不做函数复杂度分析）
- **审查基线**: git HEAD `e46d186d`（`sample_solution` 分支，"第11轮修订"，atframe_utils 子模块 `fb79c1d`），工作区干净
- **审查方式**: 全量静态审查（逐 diff 复核 + 所有结论人工核对到框架/库源码）；未运行构建、clang-tidy、cpplint
- **上一轮报告**: 基线 `1fe1ee4f`（第10轮修订）

## 结论摘要

第11轮围绕"create/destroy 与普通消息一样经 WAL 同步 + 短期重复拉取不回落 DB"做了流程重设计，方向正确：**只读副本侧链路完整自洽**（delegate 回放合并、乱序按 sequence 取 max、快照元数据闭环、落后副本经 `last_removed_key` 强制走快照、占位频道按期 GC）。同时修复了上一轮 N1（空日志回归）、N2（库右值重载重算）及约 10 个低级项。

但本轮发现 **2 个高严重度缺陷**：**N1（writable 节点上 create/destroy 成员无任何赋值点）是新流程的关键断裂**——`is_available()` 在 writable 发起的频道上恒为 false，导致新建频道对所有客户端操作返回 NOT_FOUND、destroy 不落盘不设 TTL、停机保存兜底失效；N2（`task_action_pull` 的 compact 过滤条件写反）使 pull 下发结果完全颠倒。另有 1 个中级与若干低级问题。上一轮的 M1/M2 因本轮重设计而过时（见专项说明）。

---

## 🔴 N1. [高] writable 节点上 create/destroy 成员永不赋值，`is_available()` 恒 false，新流程在发起端断裂

- **位置**: `dtmq-proxysvr/data/mq_channel.cpp:1897-1933`（`set_created`）、`:1851-1884`（`set_destroyed`）；`dtmq-proxysvr/data/mq_channel_wal_handle.cpp:67-70,84-87`（delegate 对 writable 跳过）
- **成员赋值点全量核对**（已 grep 全部 `create_sequence_ =`/`destroy_sequence_ =` 等赋值）：
  1. `load(metadata, runtime)`（`mq_channel.cpp:124-131`）——仅从 DB/快照元数据恢复；
  2. `merge_created_timepoint_and_sequence`（`:1935-1945`）——调用点：`set_created` 的两个守卫分支（`is_available()` 或 `!is_writable()` 时）+ WAL delegate `create_channel`（`mq_channel_wal_handle.cpp:90`，**前置 `if (channel->is_writable()) return` 跳过**）；
  3. `merge_destroy_timepoint_and_sequence`（`:1886-1895`）——调用点同上结构（`mq_channel_wal_handle.cpp:73`，同样跳过 writable）。
  
  即：在 writable 节点上，`set_created`/`set_destroyed` 追加 kCreate/kDestroy 日志后，回放 delegate 因 `is_writable()` 被跳过，自身又不赋值，注释所称"Writable频道由 async_destroy/destroy 接口处理"实际无人处理。`create_sequence_`/`destroy_sequence_` 在 writable 上恒为 0（`load()` 只能读到由同样为 0 的成员 `dump` 出的元数据，无法自举）。唯一例外：先作为只读副本经 delegate 合并获得 create 信息、后被提升为 writable 的频道——该偶然路径掩盖了缺陷。
- **后果链**:
  1. **新建频道不可用**：`ensure_recreate_after_destroyed`（`:1649-1666`）→ `set_created` 追加 kCreate 但成员不变 → `is_available()`（`mq_channel.h:238`，要求 `create_sequence_ > destroy_sequence_`）恒 false → `task_action_send_message:65`、`task_action_pull:60`、`task_action_find_message:61` 等全部返回 `EN_ERR_DTMQ_CHANNEL_NOT_FOUND`；且每次重试都经 `make_writable_channel:337` 再次进入 `ensure_recreate` 追加一条 kCreate，**WAL 被创建日志刷屏**；
  2. **destroy 形同虚设**：`destroy():1567` 的 `saved_sequence_ >= destroy_sequence_` 恒为真（0>=0）→ 追加 kDestroy 后直接返回，**不落盘、不设 TTL、不清理 DB**；`is_destroyed()` 恒 false → `dump(metadata)`（`:251-262`）永不写销毁元数据；重复 destroy 每次追加重复 kDestroy；
  3. **停机保存兜底失效**：`need_save_db()`（`:1233`）= `is_dirty() && is_available()` 恒 false → `mq_channel_manager::stop()`/停机分支不再把脏频道推入 pending，**停机丢数据**；
  4. **重建后 TTL 残留**：`need_remove_ttl_ = is_destroyed()`（`:1658`）恒 false → 销毁-重建后 DB 旧 TTL 不会被 `remove_ttl` 清除（`:1389-1398`），记录会被旧 TTL 到期删除；
  5. **销毁-重建反复**：writable 上"销毁后 recreate"因成员不变，每次访问都重复 set_created→kCreate，副本端则正常反复销毁/恢复，状态发散。
- **建议（二选一）**:
  - **方案 A（推荐，最贴合"和普通消息一样"的设计）**：删除 `create_channel`/`destroy_channel` delegate 的 `is_writable()` 跳过（`mq_channel_wal_handle.cpp:67-70,84-87`），让 writable 也经回放合并自身状态。注意 `reset_lock` delegate 的跳过是**正确的**（锁状态由 `set_lock` 直接维护），不能一刀切；
  - **方案 B**：在 `set_created`/`set_destroyed` 的 writable 分支直接赋值（`create_timepoint_ = now; create_sequence_ = message->sequence()`，emplace 成功后取日志实际 sequence，失败则不赋值）。
  - 附带：`ensure_recreate_after_destroyed:1661` 的 `set_created(ctx, now, alloc_message_sequence())` 传入值在 writable 路径未被使用（日志经 `allocate_log_key` 自取 sequence），白烧一个 sequence，修复时建议直接传 0。

## 🔴 N2. [高] `task_action_pull` 的 compact 过滤条件写反，下发结果完全颠倒

- **位置**: `dtmq-proxysvr/logic/action/task_action_pull.cpp:97`
- **现象**:

  ```cpp
  if (channel->get_shared_wal_object()->get_log_key_compare()(compact_stateful_sequence, (*iter)->sequence())) {
    continue;
  }
  ```

  `get_log_key_compare()` 即 `std::less<int64_t>`，该条件 = `compact < seq` 时跳过——**把未压缩的新消息全部跳过，反而把 `seq <= compact`（已折叠进 custom/private data 的 stateful 日志）下发**。旧代码（`d3771841`）为 `if ((*iter)->sequence() <= compact_stateful_sequence) continue;`（正确）。对照同语义的正确写法 `dump(snapshot)`（`mq_channel.cpp:364`）：`if (!get_log_key_compare()(compact, seq)) continue;`——本次修改应是漏写了 `!`。
- **影响**: pull/RPC 拉取消息时拿不到新消息，只拿到本应隐藏的内部 stateful 日志（kUpdateCustomData/kNoop 等）。属数据面正确性缺陷。
- **建议**: 改为 `if (!channel->get_shared_wal_object()->get_log_key_compare()(compact_stateful_sequence, (*iter)->sequence())) continue;`。

## 🟡 N3. [中] `task_action_destroy_channel` 以 `true` 作为 `destroy_sequence` 调用；`async_destroy` 已无调用方

- **位置**: `dtmq-proxysvr/logic/action/task_action_destroy_channel.cpp:83`
- **现象**:

  ```cpp
  res = RPC_AWAIT_CODE_RESULT(channel->destroy(get_shared_context(), std::chrono::system_clock::from_time_t(0), true));
  ```

  r10 签名迁移残留：第三参已由 `bool alloc_destroy_message` 改为 `int64_t destroy_sequence`，`true` 隐式转为 1。当前因 N1 未暴露实际影响，但修复 N1（尤其采用方案 B 直接赋值）后，错误的 sequence=1 会污染 `merge_destroy_timepoint_and_sequence` 的语义。另外 `async_destroy`（`mq_channel.cpp:1511`）当前在全仓库已无任何调用方（WAL delegate 已改为直接 merge），属待清理的悬挂 public 接口。
- **建议**: 改为 `channel->destroy(get_shared_context(), std::chrono::system_clock::from_time_t(0), 0)`（writable 自分配语义）；删除或保留并注释 `async_destroy` 的用途。

## 🟢 N4. [低] `load()` 对 create/destroy 元数据为覆盖式赋值，存在陈旧元数据回退窗口

- **位置**: `mq_channel.cpp:122-131`
- **现象**: `destroy_timepoint_`/`destroy_sequence_`/`create_timepoint_`/`create_sequence_` 按元数据直接覆盖（非其他字段的 max-merge）。同函数 `:133` 注释明示"后面所有数据走合并逻辑，以防调用到老的load导致meta和runtime回退"，但 create/destroy 恰好不在合并保护内。场景：迁移重叠期旧 writable 与新 writable 的元数据经事件同步/快照交替到达同一副本时，create/destroy 状态可能回退抖动（如 103→102），短暂翻转 `is_available()`。
- **建议**: 对四个字段改为 max-merge（时间戳与 sequence 分别取大），与同函数其他字段一致。

## 🟢 N5. [低] proto 删除 `stateful_hash_code` 字段 9 未加 `reserved`

- **位置**: `dtmq_proxy.proto`（`SSChannelUpdateReq`）
- **现象**: 本轮直接删除字段 9 而未 `reserved 9;`。proto3 线上兼容无碍（未知字段被忽略），但字段号可能被未来新字段复用，造成新老二进制混部时语义错乱。
- **建议**: 补 `reserved 9;` 并注释原因。

## 🟢 N6. [低] `set_destroyed`/`set_created` 中 `wal_client_` 分支成为事实死代码

- **位置**: `mq_channel.cpp:1876-1877`、`:1922-1923`
- **现象**: 两个函数的 writable 分支含 `if (wal_client_) receive_hole_log else emplace_back_log`，但 writable 节点不持有 `wal_client_`（`upgrade_to_writable` reset、`maybe_create_wal_client` 仅 readonly），该分支不可达。配合新流程（本地不再模拟销毁，`send_subscribe_to_writable:1844`），建议删除以消除误导（也是上一轮 M1 的残留形态）。

---

## 新流程正确性专项分析（响应 Hint："destroy/recreate + 主从同步 + Client 同步"）

按"创建和销毁通知与普通消息一样正常同步，短期重复拉取不回落写节点/DB"的目标逐环核对：

**设计成立的部分（副本侧，已核实）**:

1. kCreate/kDestroy 作为普通 WAL 日志参与 hash 链（`calculate_hash_code` 按 `command_case` 统一处理）与 publisher 广播，结构上与"普通消息一样"一致；
2. 只读副本经事件同步回放日志，delegate 按 `log.sequence()` 合并 create/destroy（`mq_channel_wal_handle.cpp:72-74,89-91`）；日志乱序到达时 merge 按时间戳/sequence 各自取 max，`is_destroyed()` 判定（`destroy_sequence_ > create_sequence_`）与顺序无关，乱序安全；
3. recreate 时 `ensure_recreate_after_destroyed` 先追加 kCreate 再 `remove_before(now, old_log_count)` 清掉含 kDestroy 的全部历史（kCreate 计数在其后追加、恰好幸存，`:1660-1665` 顺序正确）；落后副本因 `last_checkpoint < last_removed_key` 被 publisher 强制走快照（`wal_publisher.h:720-726`），快照元数据携带 create/destroy 字段（`dump/load`，`mq_channel.cpp:251-262/:122-131`）闭环；
4. 占位频道（!auto_create 创建、未同步的只读、已销毁）经 `update_timer:2327` 的 `!is_available()` 分支在 `cache_expire_timeout` 后 GC，期间重复拉取命中内存占位不再回落 DB——"短期重复拉取不回落"成立；非 auto_create 的 destroy 对占位频道返回成功（`task_action_destroy_channel:68-70`），符合 Note.md 新增条目；
5. `destroy()` 的异步防护到位：await 后复查 `is_destroyed()`（`:1550`）、保存后复查（`:1590`）、TTL 任务内再查（`:1598`），重建竞态被正确放弃；`saved_sequence_ >= destroy_sequence_` 的"销毁信息已保存"短路在 N1 修复后语义正确（fresh alloc 的 destroy_sequence 必大于任何已保存 sequence）。

**依赖 N1 修复才能成立的部分**: 上述 1-5 在 writable 发起端的元数据生产（`dump` 的 create/destroy 字段）目前全为 0，整个闭环在发起端断裂。修复 N1 后建议按 Note.md 新清单补测："Destroy 后重新创建，Writable 可用 + Readonly 收到创建通知恢复可用"、"非 auto_create 创建的频道可正常销毁"。

---

## 既往发现复核（基线 1fe1ee4f → e46d186d）

### ✅ 本轮已修复

| 既往编号 | 发现 | 核实 |
| --- | --- | --- |
| 上轮 N1 | `load(ctx, record)` 丢失消息拷贝 | **已修复**：`mq_channel.cpp:171` 恢复 `make_strong_rc<...>(log)` 拷贝构造 |
| 上轮 N2 | 库右值 `assign_logs` 重载仍重算哈希链 | **已修复**：atframe_utils `fb79c1d` 删除该重算块，三个重载语义已统一 |
| 上轮 N3 | `destroyed_message_saved_` 检查不可达 | **已消除**：机制整体替换为 `saved_sequence_` 比较（语义见 N1 第 2 条与专项第 5 条） |
| 上轮 N4 | `on_receive_snapshot` 钳制删除/不对称 | **已消除**：快照不再写 client configure（整块移除），统一由 `reload_configure` 管理，L8/L9 一并关闭 |
| L2（部分） | destroy wait 失败文案 | **已修复**：`:1640` 现为 "wait task failed" |
| L3 | `load_from_db` 日志第二参数无信息量 | **已修复**：两条失败日志随 auto_create 分支一并移除 |
| L4 | destroy 并发重入无复查 | **已修复**：`:1550/:1590/:1598` 三处 `is_destroyed()` 复查 |
| L5 | `mutable_heartbeat()->Add()` 未判空 | **已修复**：`:1790-1795` 判空并返回 `EN_SYS_MALLOC` |
| L7 | ctor manager 参数/owner_ 注释 | **部分修复**：注释成员已删（`mq_channel.h:302`），参数仍保留未用 |
| L17 | pull 快照分支忽略选择标志 | **已修复**：`:67-69` 按 `need_configure/need_custom_data/need_private_data` 传参 |
| L19 | `chat_channel_manager` 注释 | **已修复**：`dtmq_proxy_main.cpp:53` 已改为 mq_channel_manager |
| L20 | `stateful_hash_code` 死字段 | **已修复**：字段已删（残留问题见 N5） |
| L22/L24 | proto 注释错字/语病 | **已修复**：`:32` "空值"、`:36` "赋值新值" |
| L25 | `hash_mismatch_subscribe` 未 move | **已修复**：改为完美转发（`dtmq_algorithm.h:38-46`） |
| L15（部分） | ABI 注释事实错误 | **部分修复**：注释已改为"非 C++ 11 ABI 下 O(n)"（`mq_channel_manager.cpp:176/:720`），但事实正确的注释与分支归属仍相反——O(n) 的旧 ABI 平台仍落入 add_pending 每次调 size() 的分支（见下"仍存在 L15"） |
| L21/L23 | SDK/proto 口径与类型 | **已以注释澄清**：`dtmq_client_api.cpp:207`（`!=0` 为有意区分状态码/错误码）、`dtmq_proxy.proto:192`（sint64 因负值有特殊含义），可接受关闭 |

### ⏹ 已过时（本轮重设计取代，含对本方既往评估的修正）

- **M1（readonly 本地模拟销毁被 hash 校验拒绝）**：本轮已移除"本地模拟删除"路径——`send_subscribe_to_writable:1843-1848` 不再处理 not_found，注释明确"删除频道的消息和其他消息并无不同会正常下发"。原缺陷场景被新设计整体取代，**不再构成问题**；残留死代码见 N6。
- **M2（销毁传播链经 not_found 不可达）**：同样被新设计取代——副本错过 kDestroy 广播后，重订阅经心跳检查点由 publisher 补发增量日志（含 kDestroy）或快照（元数据携带 destroy 字段）恢复，传播闭环成立（依赖 N1 修复）。上一轮"not_found 分支死代码"的判断在当前代码中已不成立：`task_action_subscribe` 的 not_found 回带虽仍不可达，但已无任何消费方，属无害防御代码。

### ❌ 仍存在

- **M3** `make_readable_channel_with_replicate_index`：残留 kNone 频道 + 本机为分布目标时误报 `EN_ERR_MAINTENANCE`，不落 `:567+` 重试路径（`mq_channel_manager.cpp:546-563`），与 `make_readable_channel`/`make_writable_channel` 行为不一致；`get_target_distribution_server_id` 无越界归一化。本轮未改动。
- **L1** load 跳过条件 `<` vs dump `<=` 边界不一致（`mq_channel.cpp:160-163` vs `:364`）：N2 修复后此项仍有意义（DB 恢复后多残留一条边界 stateful 日志）。
- **L2（残余）** "transfer: create task failed" 复制粘贴：`:565`（writable_init）、`:642`（readonly_init）、`:1468`（async_save）；`:1354` 在 async_start_transfer 属正确。
- **L11** `create_mq_channel_publisher`/`create_mq_channel_client` 的 `private_data` 死初始化（`mq_channel_wal_handle.cpp:711-730`）。
- **L12** `publisher_send_logs` 与 `publisher_send_snapshot` 错误分类不对称（`mq_channel_wal_handle.cpp:525-531` vs `:410-423`）。
- **L13** `on_log_added` lambda 返回值无语义（`mq_channel_wal_handle.cpp:227-240`）。
- **L14** 停机兜底阶段 IO 任务未启动即 erase 无重试（`mq_channel_manager.cpp:795-799`）。
- **L15（残余）** 注释已改对但事实分支仍反：旧 ABI（`size()` 为 O(n)）平台落入 add_pending 每次调 `size()` 的即时压缩分支，新 ABI 落入 tick 低频分支，与注释意图相反（`mq_channel_manager.cpp:720-727` vs `:175-184`）。
- **L16（残余）** `send_oss`/`report_channel_qty_oss` 仍为空实现 FIXME（注释中的 `pending_save_channels_` 已改为 `running_io_channels_`，`mq_channel_manager.cpp:817`）。
- **L18** `task_action_destroy_channel:54-58` 仍吞 `EN_ERR_DTMQ_INVALID_CHANNEL`（excel 配置缺失类事故被掩盖）。

---

## 已核实无问题项（含项目方 Hint 逐条核对）

1. **公共 `send_message` 可发送控制命令**：设计如此（功能扩展），`task_action_send_message.cpp:87-91` 经 `allocate_log(...command_case...)` 统一处理。**符合设计**。
2. **`task_action_subscribe` 订阅转发失败仅记日志**：后续有心跳超时重试恢复。**符合设计**。
3. **快照等价判断只比较 WAL 尾部**：`mq_channel.cpp:703-730` 仅比较 `last_sequence`+`last_hash_code`，快照承担迁移与合并、冗余快照被忽略。**符合设计**。
4. **`EN_ERR_*` 错误码均来自 `com.const.pb.h`**：`EN_SYS_*` 在 `err::` 子命名空间，双码设计自洽，无头文件包含问题。
5. **CMake 未审查、未做函数复杂度分析**。
6. destroy 系列对重复调用的幂等：`is_destroyed()` 守卫 + merge 合并（`:1538-1541/:1853-1857`），语义正确（N1 修复后完整生效）。
7. `remove_before` 在 recreate 中的计数语义：先追加 kCreate 再按旧数量移除，kCreate 幸存、落后副本经 `last_removed_key` 走快照，正确。
8. `writable_init`/`readonly_init` 的 io_task 生命周期与 task_id 比对、冷静窗口返回 `last_result_code_`，正确。
9. 库层（atframe_utils `fb79c1d`）三个 `assign_logs` 重载语义已统一；多播 internal event + 析构注销无迭代器失效问题。
10. 工程规范：版权头、protobuf prefix/suffix 包裹、导出宏、include 排序均符合仓库约定。

## 验证与跳过项

- 本次为**纯静态审查**：未运行构建、clang-tidy、cpplint、单元测试（审查任务未改动代码）。
- N1 的结论经全量赋值点枚举（grep 所有成员赋值 + 所有 merge 调用点 + delegate 守卫）交叉验证；N2 经与 `dump(snapshot)` 同语义写法对照确认。
- 再次建议：N1/N2 均属"一次冒烟即可暴露"的问题，请优先补 Note.md 测试清单中"writable 创建-发消息-销毁-重建"与"pull 消息范围"用例。

## 建议处理优先级

1. **必须立即修复**: N1（新流程发起端断裂，方案 A 一行删除即可）、N2（一个 `!`）。
2. **建议尽快修复**: N3（签名迁移残留，随 N1 修复一并处理）。
3. **择机清理**: N4-N6、M3 及残余低级项。
