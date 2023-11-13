{{- $otelcol_traces_exporters := list "spanmetrics" -}}
{{- $otelcol_metrics_exporters := list -}}
{{- $otelcol_logs_exporters := list -}}

extensions:
  health_check:
  pprof:
    endpoint: 127.0.0.1:1777
  zpages:
    # endpoint: localhost:55679

receivers:
  otlp:
    protocols:
      grpc:

exporters:
  debug:
    verbosity: normal # basic/normal/detailed
    sampling_initial: 5
    sampling_thereafter: 500
  file/rotation_with_custom_settings:
    path: ../log/otelcol-fileexporter.log
    rotation:
      max_megabytes: 10
      max_days: 3
      max_backups: 10
      localtime: true
    format: json
    # compression: zstd
{{- if not (empty .Values.telemetry.opentelemetry.trace.otlp) }}
  {{- if not (empty .Values.telemetry.opentelemetry.trace.otlp.grpc_endpoint) }}
    {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "otlp" -}}
    {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "otlp" -}}
    {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "otlp" }}
  otlp:
    endpoint: "{{ .Values.telemetry.opentelemetry.trace.otlp.grpc_endpoint }}"
    tls:
      insecure: "{{ .Values.telemetry.opentelemetry.trace.otlp.grpc_insecure | default "true" }}"
    {{- if not (empty .Values.telemetry.opentelemetry.trace.otlp.grpc_ca_file) }}
      ca_file: "{{ .Values.telemetry.opentelemetry.trace.otlp.grpc_ca_file }}"
    {{- end }}
    timeout: "{{ .Values.telemetry.opentelemetry.trace.otlp.grpc_timeout | default "10s" }}"
  {{- end }}
  {{- if not (empty .Values.telemetry.opentelemetry.trace.otlp.http_endpoint) }}

    {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "otlphttp" -}}
    {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "otlphttp" -}}
    {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "otlphttp" }}
  otlphttp:
    endpoint: "{{ .Values.telemetry.opentelemetry.trace.otlp.http_endpoint }}"
    timeout: "{{ .Values.telemetry.opentelemetry.trace.otlp.http_timeout | default "10s" }}"
  {{- end }}
{{- end }}
{{- if and (not (empty .Values.telemetry.opentelemetry.metrics.prometheus.push)) (not (empty .Values.telemetry.opentelemetry.metrics.prometheus.push.host)) }}
  {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "prometheusremotewrite" }}
  prometheusremotewrite:
  {{- if empty .Values.telemetry.opentelemetry.trace.otlp.grpc_endpoint }}
    endpoint: "{{ .Values.telemetry.opentelemetry.metrics.prometheus.push.host }}"
  {{- else }}
    endpoint: "{{ .Values.telemetry.opentelemetry.metrics.prometheus.push.host }}:{{ .Values.telemetry.opentelemetry.metrics.prometheus.push.port | default "" }}"
  {{- end }}
    tls:
      insecure: true
    target_info:
      enabled: true
{{- end }}

processors:
  batch:
    send_batch_size: 1024
    send_batch_max_size: 10000
    timeout: 10s

connectors:
  spanmetrics:
    namespace: "{{ .Values.telemetry.opentelemetry.additional_metrics_name | default "trace.metrics" }}"
    metrics_flush_interval: 15s
    histogram:
      explicit:
        buckets: [1ms, 2ms, 8ms, 16ms, 50ms, 80ms, 100ms, 250ms, 1s, 6s]
    dimensions:
      - name: deployment.environment
        default: UNSET
      - name: host.name
        default: localhost
      - name: rpc.atrpc.result_code
        default: 0
      - name: rpc.atrpc.response_code
        default: 0
      - name: rpc.method
        default: UNKNOWN
      - name: rpc.service
        default: UNKNOWN
      - name: rpc.system
        default: UNKNOWN
      - name: service.area.zone_id
        default: 0
      - name: service.instance.name
        default: UNKNOWN
      # - name: rpc.atrpc.kind
      # - name: span.name
      # - name: service.name

service:
  extensions: [health_check, pprof, zpages]
  pipelines:
    traces:
      receivers: [otlp]
      processors: [batch]
      exporters: [{{ $otelcol_traces_exporters | join ", "}}]
      {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "file/rotation_with_custom_settings" }}
      # exporters: [{{ $otelcol_traces_exporters | join ", "}}]
    metrics:
      receivers: [spanmetrics, otlp]
      processors: [batch]
      exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
      {{- $otelcol_traces_exporters = append $otelcol_metrics_exporters "file/rotation_with_custom_settings" }}
      # exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
    logs:
      receivers: [otlp]
      processors: [batch]
      exporters: [{{ $otelcol_logs_exporters | join ", "}}]
      {{- $otelcol_traces_exporters = append $otelcol_logs_exporters "file/rotation_with_custom_settings" }}
      # exporters: [{{ $otelcol_logs_exporters | join ", "}}]
