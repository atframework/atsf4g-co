---
title: 可观测性
---

# 可观测性（Telemetry / HPA）

## OpenTelemetry 封装

`src/server_frame/rpc/telemetry/` 提供 OpenTelemetry 封装：

- `rpc_trace`：tracer/span 管理，task action 自动挂载 trace（`task_action_base` 集成）；
- `rpc_global_service`、`opentelemetry_utility`：全局 meter/tracer provider；
- `exporter/`：Prometheus 导出器（file / push 两种）；OTLP 导出由 opentelemetry-cpp 自带导出器在
  `rpc_global_service` 中接入。

配置由 `svr.telemetry.config.proto` 定义，实例配置在 `logic.telemetry` 段；部署侧见
`install/cloud-native/values/default/modules/telemetry.yaml` 与 `install/otelcol/`（Collector 启动脚本，
daemon / systemd 两种模式）。

## Trace 传播

- CS 链路：atgateway 上行不带 trace，服务侧以 task action 为 span 根；
- SS 链路：`rpc_context`（`src/server_frame/rpc/rpc_context.{h,cpp}`）随 `SSMsg` 传播 trace 上下文，
  跨服务串联 span；
- DB 调用：Redis 命令作为子 span。

## Metrics 与 HPA

`src/server_frame/logic/hpa/` 实现 HPA 自动伸缩支持：

- `pull/prometheus/`：从 Prometheus 拉取指标；
- discovery provider：向 atproxy/etcd 汇报就绪状态，供 `logic_hpa_discovery_select_mode`（如 `kReady`）
  选择目标节点（dtmq 等组件按此选择副本节点）。

`router_manager_set`、dispatcher 等也暴露内置 metrics（对象数、task 数、RPC 延迟）。

## 日志

- 框架日志走 `atframe_utils` 日志模块（libatapp 日志 sink 配置在 atapp YAML）；
- 结构化日志协议：`protocol/private/protocol/log/`（`svr.mon.log.proto` 监控日志、`svr.oss.log.proto`
  OSS 日志）；
- 部署侧由 vector（K8s）或本地文件采集，`modules/vector.yaml` 配置。
