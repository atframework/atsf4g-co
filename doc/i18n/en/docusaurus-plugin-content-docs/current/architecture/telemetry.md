---
title: Observability
---

# Observability (Telemetry / HPA)

## Quick Start

1. Set trace/metrics export destinations in values' `modules/telemetry.yaml` and prepare a Collector with the corresponding otelcol configuration.
2. Generate configuration, run services, verify a trace with an RPC, and inspect built-in metrics.
3. Business actions reuse automatic tracing; use existing telemetry APIs for additional business metrics.
4. For autoscaling, configure queries/policies in `modules/hpa.yaml` and register business readiness/state checks.
   Kubernetes also needs a metrics adapter and native HPA; follow the [HPA integration steps](../whitepaper/hpa-controller).

See [Observability and Dynamic Policies](../whitepaper/observability-policy) for design and recovery, and
[application scenarios](../whitepaper/metric-driven-scenarios) for examples of metric uses in order matching, search indexes, and battle rooms.

## Local OpenObserve

`start_local_test_env.sh/.ps1` starts OpenObserve and writes an independent values layer, which `generate_config`
merges automatically. The default exporters send OTLP/HTTP Protobuf with gzip to
`http://127.0.0.1:5080/api/atsf4g`. Applications still send signals to the local otelcol first; the Collector then
writes both local files and OpenObserve instead of looping the backend through an application OTLP receiver.
The endpoint has no trailing `/`; the exporter appends `/v1/traces`, `/v1/metrics`, and `/v1/logs`.

| Signal | OpenObserve stream |
| --- | --- |
| traces | `atframework_traces` |
| metrics | `atframework_metrics` |
| logs (including cs_actor and notification groups) | `atframework_logs` |
| spanmetrics | `atframework_metrics` |

Named telemetry groups use separate loopback receivers: `hpa` metrics use `127.0.0.1:4328`, `cs_actor` logs use
`127.0.0.1:4327`, and `notification` logs use `127.0.0.1:4337`. The `hpa` group is required for built-in policy
reporting. The OpenObserve initializer writes the Prometheus HTTP API address and credential into this group.

Streams are created on their first write and use the global 14-day retention. OpenObserve exporters have distinct
component names, a 15-second timeout, exponential-backoff retries, two consumers, and a bounded 4096-item queue.
A full queue applies backpressure. Every Collector pipeline starts with `memory_limiter`, with default hard/soft
limits of 512/384MiB.

The `span_metrics/default_default` connector is enabled and emits RED metrics such as `atframework_trace.calls`
and `atframework_trace.duration`. It uses delta temporality, millisecond histograms, and exemplars. The cardinality
limit is 4096, and inactive metrics and series expire after 15 minutes. Process instance IDs are excluded from
custom dimensions to avoid restart-driven cardinality growth; the connector's default `collector.instance.id`
remains enabled to satisfy the single-writer constraint. File output remains enabled, with separate files for
ordinary metrics and spanmetrics.

See [run and deploy](../getting-started/run-deploy#local-openobserve) for local deployment and capacity details.

## Dashboard

`install/tools/dashboards/atframework-observability.dashboard.json` can be imported directly into OpenObserve v8.
The Overview tab covers request/error ratios, mean/p95 latency, expected replicas, and resource usage. RPC Analysis
covers method-level RED, business result codes, and slow spans. Process covers CPU, memory, tasks, and traces for a
selected process instance. Multi-select variables include environment, `zone_id`, service type, full process instance
name, RPC system, method, and `span_kind`, each with an All option. `SPAN_KIND_SERVER` is selected by default to avoid
double-counting client and server spans as separate RPC calls; switch it intentionally. Counts are sampled recording
spans, not guaranteed RPC totals. Business result codes are analyzed separately from `span_status`. Filters apply only
to metric families that carry the corresponding label; HPA and coroutine series are not filtered by `span_kind`.
RPC System uses the stable `rpc.system.name` attribute; the removed `rpc.service` and legacy `rpc.system` attributes are not used.

`atframework_trace_calls` is a Counter and uses `rate()`. `atframework_trace_duration_*` is a millisecond histogram;
aggregate bucket rates by `le` before `histogram_quantile`. `rpc_atrpc_result_code` is only a business result
distribution; `span_status`/`status_code` identifies trace errors. See `install/tools/dashboards/README.md` for
generation and per-query validation.

## Customization and Design

### OpenTelemetry Wrappers

`src/server_frame/rpc/telemetry/` provides OpenTelemetry wrappers:

- `rpc_trace`: tracer/span management; task actions automatically attach traces (integrated into
  `task_action_base`);
- `rpc_global_service`, `opentelemetry_utility`: global meter/tracer providers;
- `exporter/`: Prometheus exporters (file / push variants); OTLP export is wired in `rpc_global_service` via
  opentelemetry-cpp's own exporters.

Configuration is defined by `svr.telemetry.config.proto`, and instance configuration lives in the
`logic.telemetry` section; on the deployment side see
`install/cloud-native/values/default/modules/telemetry.yaml` and `install/otelcol/` (Collector startup scripts in
daemon and systemd modes).

### Trace Propagation

- CS path: clients can supply context through `CSMsg.head.rpc_trace`, which the service uses as the parent span;
  without context, the task action starts the trace. atgateway passes through business payloads without injecting business traces;
- SS path: `rpc_context` (`src/server_frame/rpc/rpc_context.{h,cpp}`) propagates the trace context along with
  `SSMsg`, chaining spans across services;
- DB calls: generated table APIs create client spans identified by table and operation names, with lower-level Redis
  primitives as child spans.

### Metrics and HPA

`src/server_frame/logic/hpa/` implements HPA autoscaling support:

- `pull/prometheus/`: pulls metrics from Prometheus;
- Discovery provider: updates this process's discovery metadata labels so that `logic_hpa_discovery_select_mode` (e.g.
  `kReady`) can select target nodes (components such as dtmq select replica nodes based on this).
- Custom policies/discovery: pull business metrics, compute parameters, and distribute policies through etcd writes/watches;
- Replica control: publish expected replicas and state indexes of still-Ready nodes, with staged `hpa_scaling_target` / `hpa_scaling_ready` changes.

State checks, object transfers, and scaling executors require business integration. Successful collection does
not mean a policy was applied. See [minimal policy integration](../whitepaper/observability-policy) and
[HPA design](../whitepaper/hpa-controller).

`router_manager_set`, dispatchers, and others also expose built-in metrics (object count, task count, RPC
latency).

### Logging

- Framework logs go through the `atframe_utils` logging module (the libatapp log sink is configured in the atapp
  YAML);
- Structured log protocols: `protocol/private/protocol/log/` (`svr.mon.log.proto` monitoring logs,
  `svr.oss.log.proto` OSS logs);
- On the deployment side, vector (K8s) or local files collect logs. Current file outputs in
  `install/cloud-native/modules/vector.yaml` and `otelcol.yaml` use `.jsonl`. Each OSS event still needs a business
  emission point; a protocol definition alone does not produce output.
