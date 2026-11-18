---
title: 可观测性
---

# 可观测性（Telemetry / HPA）

## 快速上手

1. 在 values 的 `modules/telemetry.yaml` 配置需要的 trace/metrics 导出目标，
   使用对应 otelcol 配置准备 Collector。
2. 生成配置并运行服务，使用一次 RPC 检查 trace，查看服务内置 metrics。
3. 新业务 action 沿用自动 trace；需要额外业务指标时使用已有 telemetry API。
4. 需要自动伸缩时，在 `modules/hpa.yaml` 配置查询与策略，注册业务就绪和状态检查。
   Kubernetes 还需指标适配器与原生 HPA，按[HPA 接入步骤](../whitepaper/hpa-controller)验证。

设计与故障处理见[可观测性与动态策略白皮书](../whitepaper/observability-policy)。
订单撮合、搜索索引和战斗房间的指标用途示例见[应用场景](../whitepaper/metric-driven-scenarios)。

## 本地 OpenObserve

`start_local_test_env.sh/.ps1` 启动 OpenObserve 并生成独立 values，`generate_config` 自动完成凭据合并。
默认 exporter 使用 OTLP/HTTP Protobuf + gzip 写入 `http://127.0.0.1:5080/api/atsf4g`，不经过应用的
OTLP receiver 回环转发；应用仍先把信号发给本地 otelcol，再由 Collector 同时写本地文件和 OpenObserve。
endpoint 末尾不带 `/`，路径 `/v1/traces`、`/v1/metrics`、`/v1/logs` 由 exporter 追加。

| 信号 | OpenObserve stream |
| --- | --- |
| traces | `atframework_traces` |
| metrics | `atframework_metrics` |
| logs（含 cs_actor、notification 分组） | `atframework_logs` |
| spanmetrics | `atframework_metrics` |

本地命名遥测组使用独立回环 receiver：`hpa` metrics 使用 `127.0.0.1:4328`，`cs_actor` logs 使用
`127.0.0.1:4327`，`notification` logs 使用 `127.0.0.1:4337`。`hpa` 组是内置 policy 上报的必要配置，
其 Prometheus HTTP API 地址与凭据由 OpenObserve 初始化脚本写入该组。

这些 stream 在首次写入时自动创建，全局保留期为 14 天。OpenObserve exporter 使用独立组件名，
配置 15 秒超时、指数退避重试、两个 consumer 和 4096 item 的有界队列；队列满时施加背压。
Collector 各 pipeline 首先经过 `memory_limiter`，默认 hard/soft limit 为 512/384MiB。

`span_metrics/default_default` connector 默认启用，生成 `atframework_trace.calls` 和
`atframework_trace.duration` 等 RED 指标。默认使用 delta temporality、毫秒直方图和 exemplar；
基数上限为 4096，失效指标和 series 在 15 分钟后过期。维度不包含进程实例 ID，避免重启放大基数；
保留 connector 默认的 `collector.instance.id` 以满足 single-writer 约束。文件输出继续保留，
普通 metrics 和 spanmetrics 使用不同文件。

本地部署和容量说明见[运行与部署](../getting-started/run-deploy#本地-openobserve)。

## 仪表盘

`install/tools/dashboards/atframework-observability.dashboard.json` 可直接导入 OpenObserve v8，包含 Overview、
RPC Analysis 和 Process 三个页签。Overview 展示请求量、错误比例、平均/p95 耗时、预期副本和资源使用；
RPC Analysis 展示方法级 RED、业务结果码和慢 span；Process 展示指定实例的 CPU、内存、任务与 trace。
变量支持环境、`zone_id`、服务类型、完整进程实例名、RPC 系统、方法和 `span_kind` 的多选及全选。
默认限定为 `SPAN_KIND_SERVER`，避免把 client/server 重复计为两次 RPC；可手动切换类型。计数是受采样影响的
recording span 数，不是精确 RPC 总量，业务结果码与 `span_status` 分开分析。筛选条件只作用于具有对应标签的
指标族，HPA/coroutine 不按 `span_kind` 过滤；RPC System 使用稳定属性 `rpc.system.name`，不再使用已移除的
`rpc.service` 或旧 `rpc.system`。

`atframework_trace_calls` 是 Counter，使用 `rate()` 计算速率；`atframework_trace_duration_*` 是毫秒直方图，
先按 `le` 聚合速率，再计算 `histogram_quantile`。`rpc_atrpc_result_code` 仅用于业务结果分布，
`span_status`/`status_code` 才表示 trace 错误。生成与逐条验证方法见 `install/tools/dashboards/README.md`。

## 定制与详细设计

### OpenTelemetry 封装

`src/server_frame/rpc/telemetry/` 提供 OpenTelemetry 封装：

- `rpc_trace`：tracer/span 管理，task action 自动挂载 trace（`task_action_base` 集成）；
- `rpc_global_service`、`opentelemetry_utility`：全局 meter/tracer provider；
- `exporter/`：Prometheus 导出器（file / push 两种）；OTLP 导出由 opentelemetry-cpp 自带导出器在
  `rpc_global_service` 中接入。

配置由 `svr.telemetry.config.proto` 定义，实例配置在 `logic.telemetry` 段；部署侧见
`install/cloud-native/values/default/modules/telemetry.yaml` 与 `install/otelcol/`（Collector 启动脚本，
daemon / systemd 两种模式）。

### Trace 传播

- CS 链路：客户端可在 `CSMsg.head.rpc_trace` 中传入上下文，服务侧据此建立父子 span；
  未提供上下文时由 task action 开始 trace。atgateway 透传业务消息体，不负责注入业务 trace；
- SS 链路：`rpc_context`（`src/server_frame/rpc/rpc_context.{h,cpp}`）随 `SSMsg` 传播 trace 上下文，
  跨服务串联 span；
- DB 调用：生成的表级接口创建以表名和接口名标识的 Client span，底层 Redis 原语作为其子 span。

### Metrics 与 HPA

`src/server_frame/logic/hpa/` 实现 HPA 自动伸缩支持：

- `pull/prometheus/`：从 Prometheus 拉取指标；
- discovery provider：更新本进程的服务发现 metadata 标签，供 `logic_hpa_discovery_select_mode`（如 `kReady`）
  选择目标节点（dtmq 等组件按此选择副本节点）。
- 自定义 policy 与 discovery：拉取业务指标、计算参数，并通过 etcd 写入和监听分发业务策略；
- 副本控制：发布预期副本数和仍 Ready 节点的状态索引，分阶段调整 `hpa_scaling_target` / `hpa_scaling_ready`。

状态检查、对象转移和扩缩容执行器需要按业务接入。指标采集成功不表示策略已应用，
详见[动态策略最小接入](../whitepaper/observability-policy)与[HPA 详细设计](../whitepaper/hpa-controller)。

`router_manager_set`、dispatcher 等也暴露内置 metrics（对象数、task 数、RPC 延迟）。

### 日志

- 框架日志走 `atframe_utils` 日志模块（libatapp 日志 sink 配置在 atapp YAML）；
- 结构化日志协议：`protocol/private/protocol/log/`（`svr.mon.log.proto` 监控日志、`svr.oss.log.proto`
  OSS 日志）；
- 部署侧由 vector（K8s）或本地文件采集，`install/cloud-native/modules/vector.yaml` 和 `otelcol.yaml` 的当前
  文件输出使用 `.jsonl`；具体 OSS 事件仍需对应业务发射点，协议定义本身不代表已经产出。
