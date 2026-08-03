---
title: Observability
---

# Observability (Telemetry / HPA)

## OpenTelemetry Wrappers

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

## Trace Propagation

- CS path: atgateway upstream messages carry no trace; the service side uses the task action as the span root;
- SS path: `rpc_context` (`src/server_frame/rpc/rpc_context.{h,cpp}`) propagates the trace context along with
  `SSMsg`, chaining spans across services;
- DB calls: Redis commands appear as child spans.

## Metrics and HPA

`src/server_frame/logic/hpa/` implements HPA autoscaling support:

- `pull/prometheus/`: pulls metrics from Prometheus;
- Discovery provider: reports readiness status to atproxy/etcd so that `logic_hpa_discovery_select_mode` (e.g.
  `kReady`) can select target nodes (components such as dtmq select replica nodes based on this).

`router_manager_set`, dispatchers, and others also expose built-in metrics (object count, task count, RPC
latency).

## Logging

- Framework logs go through the `atframe_utils` logging module (the libatapp log sink is configured in the atapp
  YAML);
- Structured log protocols: `protocol/private/protocol/log/` (`svr.mon.log.proto` monitoring logs,
  `svr.oss.log.proto` OSS logs);
- On the deployment side, logs are collected by vector (K8s) or local files, configured in
  `modules/vector.yaml`.
