---
title: 道具操作与变化来源
---

# 道具操作与变化来源

通用容器算法位于 `src/component/GameSharedComponent/ItemAlgorithm/`，玩家接入位于
`src/lobbysvr/service/logic/item/`。容器负责条目、数量、位置、检查与通知；Lobby 负责持久化、dirty 同步和
具体业务来源。

## 容器操作

`ItemContainer` 提供 add/sub/load/move/apply_entries 等批次流程，并通过钩子让接入类实现单条校验、落位和
字段映射。支持有限格子、无限格子和无位置三类容器。

`user_item_manager` 的多容器流程先逐项 `check_add/check_sub`，再执行 `do_operation`。任一执行阶段失败会
直接返回，不会回滚已经提交的其他容器；因此 check/do 不是跨容器原子事务。邮件附件领取通过标记已领取
防止已提交部分被重复发放。

## 操作原因与业务来源

三个概念不能混用：

| 类型 | 内容 | 用途 |
| --- | --- | --- |
| `ItemOperationReason` | `kAdd`、`kSub`、`kLoad`、`kMoveSub`、`kMoveAdd` 等 | 容器判定的机制原因 |
| `ItemOperationSource` | `major_reason`、`minor_reason`、`reason_param` | 调用方传入的业务来源 |
| `EnItemChgMajorType` | 使用、出售、购买、邮件附件等枚举 | 业务主来源的取值 |

`ItemOperationContext` 同时携带容器原因和业务来源。来源随 `on_item_added` 等 ItemAlgorithm 回调传播。
当前生产代码中，邮件附件是明确设置非 `UNKNOWN` 来源的调用点：
`EN_ITEM_CHG_MAJOR_MAIL_ATTACHMENT + mail.minor_type + mail_id`。其他主要操作仍可能使用默认
`UNKNOWN`，不能假设所有道具变化都有完整业务来源。

## dirty 同步边界

Lobby 的 `on_item_changed` 和 `on_item_count_changed` 当前丢弃操作 context，客户端 dirty 数据只包含容器和
道具计数。也就是说，来源已经传入算法回调，但不会随现有 dirty 协议自动下发，也不代表已经完成持久化或
遥测流水。

## OSS 流水边界

`com.struct.item.common.proto` 另有 `EnItemFlowReasonMajorType`、`EnItemFlowReasonMinorType` 和
`EnItemFlowReason`；其中 major 枚举当前只有 `INVALID`。`svr.oss.log.proto` 虽定义 `OSSItemFlow`，当前
`src/` 没有实际发射点。

因此，`ItemOperationSource` 是来源传播接口，不是已经接通的 OSS 道具流水系统。新增流水时应明确映射
业务来源、容器原因和 OSS 协议字段，并在真实日志出口验证，不能只填写 context 就宣称流水完成。
