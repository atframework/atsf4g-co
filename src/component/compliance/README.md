# 合规服务

`compliance` 当前提供批量文本过滤，服务类型为 `kComplianceSvr = 33`。

## 文本过滤

`ComplianceService/filter_text` 接收 `SSFilterTextReq.items`，每条包含 UTF-8 文本及设置：是否返回原文、命中区间的替换文本（空字符串表示删除）。`SSFilterTextRsp.results` 按请求顺序返回原文（可选）、过滤文本和 `OK/MATCHED` 状态。重叠命中合并替换，相邻命中分别替换；失败不返回部分结果。

包含 `rpc/compliance/compliance_client_api.h`，调用 `rpc::compliance::filter_text(ctx, request, response)`。SDK 自动选点并一次发送整个批次；失败时不主动清空 `response`，空批次直接成功。聊天在过滤成功且有结果时按状态处理；服务故障或无结果时发送原文，命中且替换文本为空时拒绝发送。调用方与服务端需同步升级批量协议。

## 词库

资源文件为 `install/resource/compliance/text_filter.acv2`，构建配置时自动复制到 `<发布目录>/resource/compliance`。配置项 `compliance_svr.text_filter_dictionary_file` 默认指向 `../../resource/compliance/text_filter.acv2`，相对于进程工作目录。

`text_filter_manager` 管理词库加载、重载和清理。启动加载失败时报错；重载失败保留旧词库，每批使用同一快照。词库跳过 Unicode 空白、不可见字符、标点和符号，忽略 ASCII 大小写。

更新词库时，用 `ac_automation_tool` 生成到尚不存在的新文件，校验后替换资源文件：

```text
ac_automation_tool build --input <UTF-8词表> --output <新词库> --nocase --skip-space --skip-invisible --skip-punctuation --skip-symbols
ac_automation_tool verify --input <新词库> --semantic
```

当前词库来自最终合并去重的 51,758 条词表，大小写归一化后为 51,506 条。测试词库用 `test/data/text_filter_words.utf8.txt` 按同样命令生成，保存为 `test/data/text_filter_fixture.acv2`。

## 验证

开启 `PROJECT_ENABLE_UNITTEST` 和 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`，构建合规服务与 SDK 测试后运行 `ctest --test-dir <BUILD_DIR> -L compliance --output-on-failure`。测试不依赖词库工具。
