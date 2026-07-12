# 分布式消息队列服务 Review

- [ ] 需要设计抽象client端进程内共享channel的组件
- [ ] Reload后只读副本数量如果减少，要transfer多余的只读副本的订阅者到其他节点
  - [ ] 订阅者需要注意如果收到非当前订阅来源的消息，需要忽略并发送反订阅
- [ ] 负载转移窗口期，client端应该只发送消息到 kReady 节点，但本地 channel 需要检测实际channel位置
  - [ ] 如果本地不存在 -> 发送到 kTarget 节点。kTarget 节点要能接受从DB拉取数据。
  - [ ] 如果本地存在，正在转移，要等IO任务结束。
    - [ ] 未转移 -> 当前节点处理
    - [ ] 已转移 -> 发送到 kTarget 节点
  - [ ] 负载转移时，readonly节点需要合并订阅者信息。合并数据。
    - [ ] 判断本地数据如果领先于传过来的数据，直接忽略数据。
    - [ ] 如果发生缩容，会合并其他副本的订阅信息。如果由client端触发了重新负载均衡，由客户端反订阅来移除冗余订阅。
- [ ] readonly和writable可能指向相同的节点。此时以writable节点为准，不允许同步日志

## 测试清单

- [ ] 增加readonly副本数，新节点被正确拉起
- [ ] 减少readonly副本数，裁撤节点的订阅者被正确合并进现有节点
- [ ] Writable迁移
  - [ ] 迁移期间Client数据发往老节点，正常处理
  - [ ] 迁移后Client数据发往老节点，正常转发到新节点
  - [ ] 迁移后Client数据发往新节点，正常处理
- [ ] Readonly迁移
  - [ ] 迁移期间Client数据发往老节点，正常处理
  - [ ] 迁移后Client数据发往老节点，正常转发到新节点
  - [ ] 迁移后Client数据发往新节点，正常处理
- [ ] Readonly 升级到 Writable
  - [ ] 合并订阅者
  - [ ] 如果从Writable转移而来（load_snapshot），直接覆盖数据（但是如果数据已经是最新可以跳过数据重建）
  - [ ] 如果从DB读取（load_from_db），合并数据（本地比DB数据新则忽略DB数据）
- [ ] Writable 降级到 Readonly
  - [ ] 合并订阅者
- [ ] Readonly 和 Writable 迁出一段时间后都要正确被清理
- [ ] Writable 往 Readonly 副本发出Log同步事件时，如果Readonly 副本所在节点没有channel，则自动反订阅。后续不再发送这个副本的事件同步
