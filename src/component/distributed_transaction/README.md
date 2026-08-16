# 分布式事务实现

+ dtcoordsvr: 协调者服务
+ sdk: 参与者和发起者接口
+ protocol: 相关协议描述

> 请设置自动清理时间。大于容忍值+最大事务等待时间即可。

## 公开契约要点

- 协调者持久化的 `COMMITED`/`REJECTED` 是全局结果的唯一真相；client 和参与者后续错误不能反转全局结果，
  只能进入有界重试（`resolve_max_times`/`resolve_retry_interval`）和最终本地消费。
- 参与者每个恢复阶段（query、本地终态动作、participant ack）各自使用独立的有界重试预算，
  业务动作（`do_event`）成功执行至多一次。
- `force_commit` 是 best-effort 模型，不是可容灾 2PC：不创建协调者记录、参与者不进入 running/finished、
  SDK 资源锁不生效；补偿（undo）只存在于 client 本次调用的有界重试内，client/参与者故障可能永久部分执行。
  因此 `do_event`/`undo_event` 必须幂等，`undo_event` 必须支持 no-op（未执行过时成功返回）和重放。
- 参与者的 SDK 资源锁采用 Wound-Wait：被抢占（wound）的事务会被禁止 commit 并以
  `EN_TRANSACTION_RESOURCE_PREEMPTED` 失败；`check_lock` 与锁登记之间允许切出，调用方必须容忍该窗口。
- `transaction_api` 全部接口为单次调用，不含 IO 重试：`EN_DB_OLD_VERSION`/`EN_DB_KEY_EXISTS` 等可重试错误由外层
  （client 的 create/commit/reject 有界重试、参与者 acknowledge timer 到期重新拉起 resolve task）处理。
- 多副本响应合并时若出现相反终态（`COMMITED`/`REJECTED` 冲突），打印错误日志后仍按确定性规则收敛到较后终态
  （`COMMITED` 优先，与"成功可能已被 client 观察到"保持一致），不会因为拒绝合并而永远无法修正。
- 协调者 DB TTL 为绝对截止时间 `expire_timepoint + transaction_expire_grace_duration`（默认 5s），晚到保存不会
  重新获得一整段 grace；TTL 不叠加恢复预算，且不超过 `transaction_max_ttl`（默认 30 天，硬上限 3 年）。
- 协调者 LRU 缓存不延长生命周期：到期/超容量即淘汰（置 `removed` 标记，在途 IO 结束后立即失效且不会写回）；
  `memory_only` 事务也允许容量淘汰（设计允许一定程度不一致，client 会重新提交状态），淘汰时打印警告日志。
- 历史命名（`commit_transcation`、`EnDistibutedTransactionStatus`、`COMMITING/COMMITED`、
  `FINISHED == REJECTING == 2` 别名）属于公共 proto/API 兼容面，按兼容性迁移处理，不在修复中直接改名。

