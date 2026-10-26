# 合规服务

`compliance` 组件统一承载敏感词过滤、实名认证、防沉迷等的管理。

## 构建与启动

- 协议：`components::compliance-protocol`，配置协议：`components::compliance-svr-config`。
- SDK：`components::compliance-sdk`，由 `generate_for_pb_add_ss_service` 生成调用接口。
- 服务：`components::compliance-svr`；Windows 构建目标为 `pc-compliance-svr`，其他平台为 `<PROJECT_NAME>-component-compliance-svr`。
- 服务产物：`<BUILD_DIR>/publish/compliance-svr/bin/compliance-svrd[.exe]`。
- 服务类型：`logic_service_type::kComplianceSvr = 33`，服务发现名称为 `compliance-svr`。
- 配置节点：`compliance_svr`，环境变量前缀：`ATAPP_COMPLIANCE_SVR`；配置消息目前为空。
- 部署 Chart：`install/cloud-native/charts/compliance-svr`，复用 `libapp` 生成配置及启动、停止、重启、重载脚本。

## 敏感词过滤

RPC：`atframework.compliance.ComplianceService/filter_text`。

生成的 SDK 头文件为 `rpc/compliance/complianceservice.atfw.gen.h`，调用函数为 `rpc::compliance::filter_text`。
调用方选择目标服务节点，并通过 `RPC_AWAIT_CODE_RESULT` 等待结果。

请求 `SSFilterTextReq`：

- `text`：待过滤的 UTF-8 文本。
- `settings.return_original_text`：是否返回原文，默认 `false`。
- `settings.replacement_text`：将每个命中的敏感词整体替换成该文本；空字符串表示删除命中词。

响应 `SSFilterTextRsp`：

- `original_text`：只有请求要求返回时才填充原文；未返回或原文为空时均为空字符串。
- `filtered_text`：替换后的文本；没有敏感词时与请求文本相同。
- `status`：`EN_TEXT_FILTER_STATUS_OK` 或 `EN_TEXT_FILTER_STATUS_MATCHED`。

`MATCHED` 表示命中过滤规则，对应发现敏感词；RPC 是否执行成功由框架错误码表达。

以上是待实现的接口契约。目前处理函数只保留 TODO，并在 RPC 回包头返回 `EN_SYS_NOT_SUPPORT`，不填充过滤结果。
调用方必须先检查 RPC 错误码，成功时才读取业务状态和文本。词库、匹配规则及其他合规能力由后续实现补充。

## 验证

开启 `PROJECT_ENABLE_UNITTEST` 和 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS` 后，构建
`<PROJECT_NAME>-component-compliance-unit-test`，运行 `ctest --test-dir <BUILD_DIR> -L compliance --output-on-failure`。
用例通过真实 RPC 注册和 dispatcher 分发请求，检查占位接口返回 `EN_SYS_NOT_SUPPORT`、不填充业务结果，并且只发送一次回包。
