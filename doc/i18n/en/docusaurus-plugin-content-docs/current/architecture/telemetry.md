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
[application scenarios](../whitepaper/metric-driven-scenarios) for order matching, search indexes, and battle rooms.

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

- CS path: atgateway upstream messages carry no trace; the service side uses the task action as the span root;
- SS path: `rpc_context` (`src/server_frame/rpc/rpc_context.{h,cpp}`) propagates the trace context along with
  `SSMsg`, chaining spans across services;
- DB calls: Redis commands appear as child spans.

### Metrics and HPA

`src/server_frame/logic/hpa/` implements HPA autoscaling support:

- `pull/prometheus/`: pulls metrics from Prometheus;
- Discovery provider: reports readiness status to atproxy/etcd so that `logic_hpa_discovery_select_mode` (e.g.
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
- On the deployment side, logs are collected by vector (K8s) or local files, configured in
  `modules/vector.yaml`.
