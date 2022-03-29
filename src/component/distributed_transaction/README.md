# 分布式事务实现

+ dtcoordsvr: 协调者服务
+ sdk: 参与者和发起者接口
+ protocol: 相关协议描述

> 请设置tcaplus表中 `TABLE_DISTRI_TRANSACTION_DEF` 的自动清理时间。大于容忍值+最大事务等待时间即可。
