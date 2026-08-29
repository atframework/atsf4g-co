# dtmq 待办：writable transfer 的事件可见延迟优化

背景（2026-08-29 决策记录，teamsvr-room WAL-06 用例观察到的生产契约）：

writable transfer 后，新可写节点上 `mq_channel::load_snapshot` 把 publisher 的广播边界
（`broadcast_key_bound`）重置到最后一条已加载日志。checkpoint 比该边界更早的订阅者，
随后收到的新日志增量批次会跳过中间缺失的日志，被客户端 `wal_client` 的哈希链校验拒绝
（`kHashCodeMismatch`），只能等待下一次订阅心跳从自身 checkpoint 起连续补齐。按当前
`heartbeat_interval` 配置（如 300s），转移后订阅者最长一个心跳周期看不到新事件。

已确认可接受的范围：转发心跳失败导致的心跳周期内延迟收不到事件。

## 待办事项

1. **transfer 时尽可能携带当前订阅者的 checkpoint 数据**

   writable transfer 的快照（`SSChannelTransferChannelReq.channel_snapshot`）已经携带存活
   订阅者列表。目标节点 `load_snapshot` 合并订阅者时，应尽可能利用快照中的订阅者
   checkpoint/broadcast 位置信息，使转移完成后新 publisher 能直接从各订阅者的进度继续
   增量下发，而不是统一从"最后一条已加载日志"起广播。

2. **transfer 后收到新 message 时由新 writable 直接通知订阅者**

   结合上一条：新可写节点收到新 message 后，对 checkpoint 已知的订阅者应直接补发其
   checkpoint 之后的增量（等价于把这些订阅者视为待补发状态），避免依赖订阅者自身心跳
   触发 catch-up。

3. **transfer 期间收到新的订阅心跳时尝试转发一次**

   转移窗口内，旧可写节点若收到订阅者的心跳更新（checkpoint 推进），应尝试把该心跳
   转发（一次）到转移目标节点，使新可写节点上的订阅者进度尽量新鲜。转发失败时接受
   心跳周期内的补齐延迟（订阅者下次心跳自愈），不引入重试风暴。
