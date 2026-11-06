---
title: 合规服务
---

# 合规服务

`compliance` 提供 UTF-8 文本批量过滤。服务类型为 `kComplianceSvr = 33`，部署 chart 为
`compliance-svr`。服务实现、SDK 和协议分别位于 `src/component/compliance/compliance_svr/`、
`src/component/compliance/sdk/` 和 `src/component/compliance/protocol/`。

## 调用链与部署

消费方链接 `compliance-sdk`，包含 `rpc/compliance/compliance_client_api.h`，调用
`rpc::compliance::filter_text(ctx, request, response)`。SDK 从服务发现中选择就绪节点，使用第一条文本作为
一致性哈希路由键，并一次发送整个批次。

部署时启用 `compliance-svr` 实例，并确保发布目录包含 `resource/compliance/text_filter.acv2`。
`compliance_svr.text_filter_dictionary_file` 配置词库路径，默认值相对于进程工作目录。

## 批量协议

`ComplianceService/filter_text` 的 `SSFilterTextReq.items` 中，每项包含待过滤文本和设置：

- `return_original_text`：是否在结果中返回原文。
- `replacement_text`：命中区间的替换文本；空字符串表示删除命中区间。

`SSFilterTextRsp.results` 与请求条目按下标一一对应。`status` 为 `OK` 或 `MATCHED`，`filtered_text`
保存过滤结果，未命中时与输入相同。重叠命中合并后替换，相邻命中分别替换。任一条目处理失败时，服务端
返回错误并清除结果，不返回部分成功条目。

空批次由 SDK 直接成功返回并清空 `response`。其他 SDK 调用失败不会主动清空 `response`，调用方必须检查
RPC 结果，不能把旧响应当作本次过滤结果。是否放行未过滤文本由调用方策略决定。

## 词库与重载

`text_filter_manager` 负责词库加载、重载和清理。启动加载失败会报告错误；重载失败保留旧词库；每批处理
使用同一份快照。匹配忽略 ASCII 大小写，并可跳过经保守白名单筛选的标点、空白和格式字符。组合标记、
连接控制符及其他语言的正常文字组成部分不能被无条件跳过。

更新词库时，用 `ac_automation_tool` 生成到尚不存在的新文件，校验后再替换资源文件：

```text
ac_automation_tool build --input <UTF-8词表> --output <新词库> --nocase --skip-space --skip-invisible --skip-punctuation --skip-symbols
ac_automation_tool verify --input <新词库> --semantic
```

完整 skip 规则和词库维护说明见 `src/component/compliance/README.md`。

## 接入

在消费服务的现有声明中添加：

```cmake
USE_COMPONENTS "compliance-sdk"
```

调用方需要同时处理三种结果：未命中、命中并已生成替换文本、RPC 或结果不可用。替换策略是否允许继续
发送原文属于业务决策，不应由 SDK 隐式决定。

## 验证

开启 `PROJECT_ENABLE_UNITTEST` 和 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`，构建合规服务与 SDK
测试后运行：

```bash
ctest --test-dir <BUILD_DIR> -L compliance --output-on-failure
```

测试使用 `src/component/compliance/test/data/` 中的小型词库，不依赖词库生成工具。
