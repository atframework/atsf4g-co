---
title: 邮件
---

# 邮件

邮件属于 `lobbysvr` 和 `lobbysvr-sdk`，不存在独立的 `mailsvr`。实现包括个人/全服邮件管理器、Redis
记录、`Mail.xlsx` 模板、发放 SDK、CS 同步和 robot 命令。

## 发放 SDK

`src/lobbysvr/sdk/rpc/mail/` 提供以下 RPC 适配接口，返回值必须通过项目 RPC await 宏消费：

- `add_user_mail`、`add_user_mail_with_template`；
- `add_global_mail`、`add_global_mail_with_template`；
- `remove_global_mail`。

两个模板发放接口都有 `after_rendered` 重载。回调在模板渲染和邮件 ID 分配完成后、写入邮件前同步执行，
可修改 `DMailContent`，用于自定义标题、正文或其他内容。参数是 `function_ref`，调用方必须保证回调对象在
本次调用期间有效，不能把它保存到调用之后使用。

消费方使用现有 `USE_SERVICE_SDK "lobbysvr-sdk"` 依赖。个人邮件要求有效的 `major_type` 和收件人用户 ID；
未提供的时间字段会按投递时间和默认过期配置补齐。

## 存储与刷新

个人邮件先写 `mail_content`，再通过用户异步任务插入邮件索引。若索引已存在，会清理本次未关联的内容并
按幂等成功处理。内容记录设置保底 TTL，避免异常路径泄露。

全服邮件由 `mail_global_index` 和邮件内容记录保存，按 `(zone_id, major_type)` 管理索引并执行容量压缩。
`zone_id=0` 表示跨区全服邮件。变更后通知相关 Lobby 刷新；内部 `return_updated=false` 路径只刷新其他
Lobby，调用方需要自行维护本地缓存。

## 模板

`resource/ExcelTables/Mail.xlsx` 定义客户端和服务端模板。`render_by_client` 决定由哪一侧渲染；
服务端模板引擎支持 `${var}`、`${format(...)}` 和嵌套函数。模板 ID 使用
`[600000, 700000)` 区间，当前示例包含 `600001`、`600002`。

SDK 的模板重载只暴露固定扩展参数；`src/lobbysvr/sdk/logic/mail/mail_utils.h` 中的
`mail_render_template` 还支持内部扩展映射和附件参数。新增模板语法或字段时应检查
`mail_setup_template_engine` 及实际渲染输出，不能只验证生成器退出成功。

## CS 接口与同步

当前客户端接口只有：

- `mail_get_all`；
- `mail_read`、`mail_read_all`；
- `mail_receive_attachment`、`mail_receive_attachment_all`；
- `mail_change_sync`。

不要把规划名称或生成模板当成已有的通用客户端发信、删信或批量删除接口。

## 附件与部分提交

邮件附件只支持 `DItemBasic` 数量数据；携带实例 `data` 的附件返回参数错误。领取时先检查再执行道具操作，
但多容器操作不是全局事务：中间失败不会回滚此前容器。已提交的附件必须标记为已领取，不能通过重试再次
发放。

成功领取会设置 `EN_ITEM_CHG_MAJOR_MAIL_ATTACHMENT`，以邮件 `minor_type` 为次要来源、`mail_id` 为
`reason_param`。详见[道具操作与变化来源](item)。默认 `need_remove=true`，即使空邮件或空附件操作也会
按流程标记移除。

排行榜结算目前仍由 `rank_settlement_svr/service/settle_rank` 的占位入口表示，没有自动调用邮件或道具
发放；接入方需要实现实际奖励流程。
