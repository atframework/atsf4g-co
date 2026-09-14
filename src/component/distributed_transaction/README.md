# 分布式事务实现

+ dtcoordsvr: 协调者服务
+ sdk: 参与者和发起者接口
+ protocol: 相关协议描述

> 请设置自动清理时间。大于容忍值+最大事务等待时间即可。

## 公开契约要点

- 协调者持久化的 `COMMITED`/`REJECTED` 是全局结果的唯一真相；client 和参与者后续错误不能反转全局结果，
  只能进入有限次重试（`resolve_max_times`/`resolve_retry_interval`）并最终在本地完成清理。
- 参与者每个恢复阶段（query、`do_event`、`on_finished`、participant ack）各自使用独立的有限次重试，
  快照重载保留当前阶段的计数。`do_event`、`on_finished` 必须幂等，业务数据与 SDK 快照须由接入方一致保存。
  已确认提交后的本地动作仍可能因重试耗尽而被清理，因此 client 成功表示全局决议为提交，不保证所有业务动作已成功。
  回调须在内部捕获 C++ 异常并返回错误码。当前协程框架不把未捕获的 `throw` 转为 RPC 错误，异常越过
  `noexcept` 的任务启动或取消边界会终止进程；SDK 的重试与清理契约适用于返回错误、任务超时和取消。
- 同 UUID 的 `create` 重放保留已有终态和参与者确认；内存模式复用已有缓存。
  持久化模式沿用 `insert → set_ttl`，TTL 失败时尝试删除新记录并返回原错误；保存沿用 `replace`。
  这些独立 DB 调用不提供跨进程原子性保证。
  UUID 必须全局唯一，同 UUID 请求必须属于同一事务；记录清理后的去重需要业务层保证。
- `force_commit` 是 best-effort 模型，不是可容灾 2PC：不创建协调者记录、参与者不进入 running/finished、
  不创建恢复定时器；补偿（undo）只存在于 client 本次调用的有限次重试内，client/参与者故障可能永久部分执行。
  因此 `do_event`/`undo_event` 必须幂等，`undo_event` 必须支持 no-op（未执行过时成功返回）和重放。
  undo 请求要求内部 UUID 非空且与外部一致，否则返回参数错误。
  `lock_resource` 在 `on_start_running` 前加锁，持续到 `do_event` 返回后、`on_finish_running` 前；动作失败或任务退出也会解锁。
  显式调用 `lock` 会登记资源锁；直接调用者须在本次调用结束前用 storage 句柄 `unlock`。
  `on_finished` 失败仍只记录日志并继续完成通知，不进入普通事务的恢复重试。
- 参与者的 SDK 资源锁采用 Wound-Wait：冲突时保留原锁，不登记竞争者的新锁。
  全部资源通过年龄和状态检查后，才为可抢占的持锁事务登记 wound；存在不可抢占资源时不 wound 其他事务。
  prepare 冲突时返回 `EN_TRANSACTION_RESOURCE_PREEMPTED` 和
  `allow_retry=true`，client 沿用原 UUID、时间戳和已准备的参与者重试。
  wound 独立保存在快照的 `wounded_transaction_uuid` 中，保持 `PREPARED`、原恢复时间和已消耗的重试次数。
  `REJECTING`/`REJECTED` 表示已进入拒绝流程，后续误发 commit 不能改变该方向。
  收到 commit/reject 通知时按已确认决议执行；否则超时后请求全局 `reject_transaction`，沿用配置的 R 个成功响应要求。
  已提交则按提交方向完成。成功响应不足 R 或 NOTFOUND 不能确认拒绝；超时恢复的重试次数达到上限后清理本地记录，不发送拒绝 ACK。
  `check_lock` 与锁登记之间允许切出，`lock` 会重新检查事务年龄和状态；同一事务重复加锁保留完整的快照资源列表。
  普通模式仅允许当前 running 对象加锁；快照重载前的旧对象不能重新登记锁或 wound 其他事务。
  `check_writable` 及其 vtable 回调同步返回 `int32_t` 错误码，不切出协程。
  `expire_timepoint` 触发恢复；恢复任务的 `check_writable` 报错或不可写时，每个未处理事务的当前阶段重试计数加一。
  重试发生在收到决议或超时之后，达到 `max(1, resolve_max_times)` 后可清理，不必再次等待过期时间。
  不可写时的强制清理不执行业务或完成回调，也不发送 participant ack。
  ACK 批次在处理后续条目前重新检查可写性，前一个 IO 切出期间失去写权限时停止派发后续动作。
  恢复任务进入退出状态（kill/超时/取消）后不再处理剩余条目：未处理条目按退避重新排期，不增加重试计数。
  在途回调返回后不能改写 SDK 的新事务对象；已发出的业务操作仍需由接入方校验资源所有权或版本。
- 生命周期回调切出前完成 running 到 finished 的登记，重复 prepare/commit 不会再次执行业务动作。
  两种模型首次 prepare 通过检查后均将 `CREATED` 推进到 `PREPARED`，再触发 `on_start_running`。
  普通模式在 `on_start_running` 返回后才登记原截止时间的恢复定时器；回调在途的 commit/reject 返回
  `EN_SYS_BUSY`，不会提前执行业务动作或完成事件。启动回调报错或任务超时后仍可恢复；快照重载不重放启动回调。
  普通事务的 `commit_participator`/`reject_participator` 成功后，才将本地 `COMMITING`/`REJECTING` 推进到对应终态，
  并单向合并服务端状态和完成时间。回调及是否设置 `do_event` 不决定终态。
  查询已获知的终态直接保留；`load` 原样恢复状态，不因本地执行或 ACK 尚未完成而回退。
  `on_finished` 成功后记录 `finished_callback_completed` 并启动 ACK；失败重试耗尽则清理记录，不发送 ACK。
  running 恢复本地动作；finished 根据回调完成标记恢复回调或 ACK，ACK 重试不重放已成功的回调。
  已知终态仍可重复发送同方向 ACK，由协调者保证幂等。
  ACK 失败（含未达到副本成功数）时不应用部分响应，下一次确认沿用当前已知状态。
  快照重载后，旧查询、本地动作和 ack 结果按对象身份校验，不能改写新对象或替换其恢复定时器。
- `on_commited`/`on_rejected` 返回后才登记 ACK 定时器，避免通知切出期间被 ACK 推进状态或删除记录。
  恢复批次逐条释放已处理的 storage，后续事务的 IO 或回调等待不会继续持有已清理对象。
  不可写或任务退出时，未处理条目清理或重新排期后立即释放批次引用，不等任务完成回调返回。
- client 在派发每个 prepare 前及最后一个 prepare 返回后检查原截止时间。
  协调者忙时的 create/commit 重试也检查该截止时间，超时后停止新请求；确认全局结果的 query 和拒绝收尾仍可继续。
  到期后不再派发 prepare 或主动提交；已准备成功的参与者按协调者确认的决议完成通知，
  `force_commit` 则进入本次调用内的补偿。两种模式都不延长原事务截止时间。
  参与者 key 不允许为空字符串（`add_participator` 拒绝）；
  无失败者时不向未 prepare 的参与者补发 undo/reject。
- client 的一次 `submit_transaction` 固定持有入口处的 storage，回调替换调用者指针不会改变当前事务，
  该持有在本次调用结束时释放。同一 storage 在途重入返回 `EN_TRANSACTION_ALREADY_RUN`，不改写事务和输出集合，
  包括通过不同 handle 提交；同一 handle 可同时提交不同 storage。
  前一次调用已结束且全局决议未确认时，仍可重新提交 `PREPARED` 事务。
  客户端 `storage_type` 包装 protobuf 数据（`data`）和私有提交标记，提交标记不参与序列化。
  client handle 本身须由调用方保持有效，直到调用结束。提交入口按单线程协程使用，不提供线程同步。
- `transaction_api` 全部接口为单次调用，不含 IO 重试：`EN_DB_OLD_VERSION`/`EN_DB_KEY_EXISTS` 等可重试错误由外层
  （client 的 create/commit/reject 有限次重试、参与者 acknowledge timer 到期重新拉起 resolve task）处理。
- 多副本响应合并时若出现相反的最终状态（`COMMITED`/`REJECTED` 冲突），打印错误日志后仍按确定性规则收敛到较后的最终状态
  （`COMMITED` 优先，与"成功可能已被 client 观察到"保持一致），不会因为拒绝合并而永远无法修正。
  未达到副本成功数要求时的部分响应不能作为 client 的全局决议，须重新查询确认后才能发送最终状态通知。
  当前读写调用均等待 R 个有效成功响应，未单独配置 W；要保证与已完成写入相交，配置须满足 `2R > N`。
  单节点（非复制）模式的成功响应必须携带 `metadata`/`storage`，缺失时返回 `EN_SYS_UNPACK`，不清空调用方已有数据。
  query 可复用输出对象；复制查询仅合并本次收到的副本，不混入上次查询的事务、状态或业务数据。
  复用 reject 请求时，普通模式会清除之前的 `force_commit` 补偿 storage。
- 协调者 DB TTL 按 `expire_timepoint + transaction_expire_grace_duration`（默认 5s）的剩余秒数计算，晚到保存不会
  重新获得一整段 grace；TTL 不叠加恢复流程的重试等待时间，且不超过 `transaction_max_ttl`（默认 30 天，硬上限 3 年）。
  Redis 接收相对秒数，实际删除时间还受秒级取整、请求排队及 Redis 过期清理影响。
- 协调者 LRU 按到期时间和容量淘汰已完成 IO 的条目，并使旧句柄失效。在途 IO 保留缓存入口，完成后恢复正常淘汰；
  `lru_max_cache_count = 0` 表示不限容量，仅按 `lru_expired_duration` 过期淘汰，缺省配置不会清空缓存。
  同一协调者进程内，同 UUID 的创建、读取、保存和删除通过缓存对象的 `io_task` 串行执行。
  等待者醒来后重新检查当前 IO 和缓存身份；调用者超时不清除仍在执行的子任务，IO 收尾时才清理失败数据或空占位。
  删除记录（参与者 ACK 收尾、强制 remove）先等待在途 IO 完成，再以当前缓存记录的 `memory_only` 和复制配置
  决定是否删除 DB 及使用哪个分区；强制 remove 在没有缓存时使用请求 metadata。
  非复制事务使用分区 0，复制事务使用本服务分区。参与者 ACK 的原缓存已失效或被替换时，返回 `EN_SYS_NOTFOUND`。
  内存事务只移除缓存；持久化事务在 DB 删除成功或记录已不存在后移除缓存，并使旧句柄失效。
  DB 删除失败时保留已有数据的缓存，供重试与后续请求直接命中；无缓存删除所建的空占位在 IO 收尾时清理。
  同一 IO 任务重入同 UUID 的管理接口返回 `EN_SYS_RPC_CALL_NOT_READY`，避免等待自身。
  DB 数据内的 UUID 必须与请求 key 一致，解码不一致返回 `EN_SYS_UNPACK`，不缓存或保存该记录。
  `memory_only` 事务也允许容量淘汰（设计允许一定程度不一致，client 会重新提交状态），淘汰时打印警告日志。
  `stop()` 拒绝新建和读取，保留已有缓存供在途任务收尾；`cleanup()` 阶段清空缓存并使旧句柄失效。
  停服时，在途创建完成 TTL 设置后清除创建占位；在途读取返回停服错误并清空输出句柄。已持久化记录仍由其 DB TTL 管理。
- 历史命名（`commit_transcation`、`EnDistibutedTransactionStatus`、`COMMITING/COMMITED`、
  `FINISHED == REJECTING == 2` 别名）属于公共 proto/API 兼容面，按兼容性迁移处理，不在修复中直接改名。

## 单元测试

启用 `PROJECT_ENABLE_UNITTEST` 和 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`，构建
[test/CMakeLists.txt](test/CMakeLists.txt) 中的六个测试目标后运行：

```sh
ctest --test-dir <BUILD_DIR> -L distributed-transaction --output-on-failure
```

测试使用真实 SDK、协调者 action、协程和生成 RPC，模拟外部 SS/DB 边界，覆盖多副本响应、状态重放、
资源锁、快照恢复、回调重入、失败重试、TTL、停服和压力场景。真实 Redis 故障转移与跨进程网络分区需另做集成验证。

生命周期回归在回调中检查状态并逐项比较事件顺序；覆盖普通模式和 `force_commit` 的动作、完成回调及
完成通知任务超时。通过弱引用、锁和定时器验证正常收尾、失败耗尽、批次内清理及停服在途 IO 的释放边界。

| 测试目标 | 主要检查 |
| --- | --- |
| API | 单节点与 R/N 副本响应、畸形 payload、输出及请求复用、单次调用错误传播 |
| Client | 两种模式的 prepare/通知顺序与回调状态、截止时间、有限次重试、重入、storage 替换和任务超时释放 |
| 参与者 | 两种模式的生命周期、锁冲突、快照恢复、启动及完成回调切出、重试耗尽、弱引用和定时器清理 |
| 协调者 | DB 与 `memory_only`、终态及 ACK 幂等、TTL、缓存淘汰、旧句柄和并发 IO |
| 协调者停服 | `stop`/`cleanup`、在途 create/query/save、输出句柄及缓存释放 |
| 压力 | 连续事务、失败重试和回调数量，结束后的 SDK 容器清理 |

