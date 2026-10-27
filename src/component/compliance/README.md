# 合规服务

`compliance` 当前提供批量文本过滤，服务类型为 `kComplianceSvr = 33`。

## 文本过滤

`ComplianceService/filter_text` 接收 `SSFilterTextReq.items`，每条包含 UTF-8 文本及设置：是否返回原文、命中区间的替换文本（空字符串表示删除）。`SSFilterTextRsp.results` 按请求顺序返回原文（可选）、过滤文本和 `OK/MATCHED` 状态。重叠命中合并替换，相邻命中分别替换；失败不返回部分结果。

包含 `rpc/compliance/compliance_client_api.h`，调用 `rpc::compliance::filter_text(ctx, request, response)`。SDK 自动选点并一次发送整个批次；失败时不主动清空 `response`，空批次直接成功。聊天在过滤成功且有结果时按状态处理；服务故障或无结果时发送原文，命中且替换文本为空时拒绝发送。调用方与服务端需同步升级批量协议。

## 词库

资源文件为 `install/resource/compliance/text_filter.acv2`，构建配置时自动复制到 `<发布目录>/resource/compliance`。配置项 `compliance_svr.text_filter_dictionary_file` 默认指向 `../../resource/compliance/text_filter.acv2`，相对于进程工作目录。

`text_filter_manager` 管理词库加载、重载和清理。启动加载失败时报错；重载失败保留旧词库，每批使用同一快照。词库忽略 ASCII 大小写，使用经过筛选的 2,847 个 skip 字符。

skip 表基于 Unicode 18.0.0 的 `Pattern_Syntax`、`White_Space`、`Cc`、双向格式控制符，以及下划线、软连字符、零宽空格、Word Joiner 和 BOM。兼容形式只有在递归分解的每个字符均属于基础白名单时才加入。再排除字母、组合标记、数字、部首、文字延长符、音调/变音符、连接控制符、变体选择符、表意文字描述符，以及兼容分解后含上述内容的字符；未分配码位和私用区不进入 skip 表。

例如 `*`、`-`、`/`、空格和中文句号会被跳过；`⼀`、`⺀`、`々`、韩文填充字、组合重音符和 ZWJ/ZWNJ 保持有意义，不会被跳过。不直接展开整个 `S*` 或 `Default_Ignorable_Code_Point`，以免吞掉其他语言的正常文字组成部分。规则依据见 [Unicode UAX #31](https://www.unicode.org/reports/tr31/#Pattern_Syntax) 和 [UAX #44](https://www.unicode.org/reports/tr44/)。

更新词库时，用 `ac_automation_tool` 生成到尚不存在的新文件，校验后替换资源文件：

```text
ac_automation_tool build --input <UTF-8词表> --output <新词库> --nocase --skip-space --skip-invisible --skip-punctuation --skip-symbols
ac_automation_tool verify --input <新词库> --semantic
```

当前词库来自最终合并去重的 51,758 条词表，大小写归一化后为 51,506 条，文件为 6,305,704 bytes。内置四类 skip 选项使用上述保守白名单，可逐字节重建当前资源。源词表、`text-filter-safe-skips.utf8.txt` 和 `text-filter-safe-skip-policy.json` 保留在构建目录的 `datasets/ac_automation_v2/2026-10-26/`，不加入 Git；策略文件记录完整码位区间、Unicode 来源和 SHA-256，也可用 `--skip-file` 显式重建相同规则。

测试词库及其小型文本来源分别位于 `test/data/text_filter_fixture.acv2` 和 `test/data/text_filter_words.utf8.txt`。

## 验证

开启 `PROJECT_ENABLE_UNITTEST` 和 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`，构建合规服务与 SDK 测试后运行 `ctest --test-dir <BUILD_DIR> -L compliance --output-on-failure`。测试不依赖词库工具。
