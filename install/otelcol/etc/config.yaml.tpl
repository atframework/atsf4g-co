{{- $otelcol_traces_exporters := list "spanmetrics" -}}
{{- $otelcol_metrics_exporters := list -}}
{{- $otelcol_logs_exporters := list -}}
{{- $otelcol_agent_data_source := dict -}}
{{- if not (empty .Values.telemetry.agent.group_name) -}}
  {{- $otelcol_agent_data_source = get .Values.telemetry.group .Values.telemetry.agent.group_name -}}
  {{- if empty $otelcol_agent_data_source -}}
    {{- $otelcol_agent_data_source = .Values.telemetry.opentelemetry -}}
  {{- end -}}
{{- else -}}
  {{- $otelcol_agent_data_source = .Values.telemetry.opentelemetry -}}
{{- end -}}

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
  file/rotation_trace:
    path: ../log/otelcol-trace.log
    rotation:
      max_megabytes: 10
      max_days: 3
      max_backups: 10
      localtime: true
    format: json
    # compression: zstd
  file/rotation_metrics:
    path: ../log/otelcol-metrics.log
    rotation:
      max_megabytes: 10
      max_days: 3
      max_backups: 10
      localtime: true
    format: json
    # compression: zstd
  file/rotation_logs:
    path: ../log/otelcol-logs.log.zst
    rotation:
      max_megabytes: 10
      max_days: 3
      max_backups: 10
      localtime: true
    format: json
    compression: zstd

{{- if not (empty $otelcol_agent_data_source.trace) }}    
  {{- if not (empty $otelcol_agent_data_source.trace.otlp) }}
    {{- if not (empty $otelcol_agent_data_source.trace.otlp.grpc_endpoint) -}}
      {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "otlp/trace" }}
  otlp/trace:
    endpoint: "{{ $otelcol_agent_data_source.trace.otlp.grpc_endpoint }}"
    tls:
      insecure: "{{ $otelcol_agent_data_source.trace.otlp.grpc_insecure | default "true" }}"
      {{- if not (empty $otelcol_agent_data_source.trace.otlp.grpc_ca_file) }}
      ca_file: "{{ $otelcol_agent_data_source.trace.otlp.grpc_ca_file }}"
      {{- end }}
    timeout: "{{ $otelcol_agent_data_source.trace.otlp.grpc_timeout | default "10s" }}"
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.trace.otlp.http_endpoint) -}}
      {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "otlphttp/trace" }}
  otlphttp/trace:
    endpoint: "{{ $otelcol_agent_data_source.trace.otlp.http_endpoint }}"
    timeout: "{{ $otelcol_agent_data_source.trace.otlp.http_timeout | default "10s" }}"
    {{- end }}
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_agent_data_source.metrics) }}    
  {{- if not (empty $otelcol_agent_data_source.metrics.otlp) }}
    {{- if not (empty $otelcol_agent_data_source.metrics.otlp.grpc_endpoint) -}}
      {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "otlp/metrics" }}
  otlp/metrics:
    endpoint: "{{ $otelcol_agent_data_source.metrics.otlp.grpc_endpoint }}"
    tls:
      insecure: "{{ $otelcol_agent_data_source.metrics.otlp.grpc_insecure | default "true" }}"
      {{- if not (empty $otelcol_agent_data_source.metrics.otlp.grpc_ca_file) }}
      ca_file: "{{ $otelcol_agent_data_source.metrics.otlp.grpc_ca_file }}"
      {{- end }}
    timeout: "{{ $otelcol_agent_data_source.metrics.otlp.grpc_timeout | default "10s" }}"
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.metrics.otlp.http_endpoint) -}}
      {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "otlphttp/metrics" }}
  otlphttp/metrics:
    endpoint: "{{ $otelcol_agent_data_source.metrics.otlp.http_endpoint }}"
    timeout: "{{ $otelcol_agent_data_source.metrics.otlp.http_timeout | default "10s" }}"
    {{- end }}
  {{- end }}
  {{- if and (not (empty $otelcol_agent_data_source.metrics.prometheus.pull)) (not (empty $otelcol_agent_data_source.metrics.prometheus.pull.url)) }}
    {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "prometheus" }}
  prometheus:
    endpoint: "{{ $otelcol_agent_data_source.metrics.prometheus.pull.url }}"
    {{- if not (empty $otelcol_agent_data_source.metrics.prometheus.pull.tls) }}
    tls:
      {{- if not (empty $otelcol_agent_data_source.metrics.prometheus.pull.tls.ca_file) }}
      ca_file: "{{ $otelcol_agent_data_source.metrics.prometheus.pull.tls.ca_file }}"
      {{- end }}
      {{- if not (empty $otelcol_agent_data_source.metrics.prometheus.pull.tls.cert_file) }}
      cert_file: "{{ $otelcol_agent_data_source.metrics.prometheus.pull.tls.cert_file }}"
      {{- end }}
      {{- if not (empty $otelcol_agent_data_source.metrics.prometheus.pull.tls.key_file) }}
      key_file: "{{ $otelcol_agent_data_source.metrics.prometheus.pull.tls.key_file }}"
      {{- end }}
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.metrics.prometheus.pull.namespace) }}
    namespace: "{{ $otelcol_agent_data_source.metrics.prometheus.pull.namespace }}"
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.metrics.prometheus.pull.resource_to_telemetry_conversion) }}
    resource_to_telemetry_conversion:
      {{- if not (empty $otelcol_agent_data_source.metrics.prometheus.pull.resource_to_telemetry_conversion.enabled) }}
      enabled: "{{ $otelcol_agent_data_source.metrics.prometheus.pull.resource_to_telemetry_conversion.enabled }}"
      {{- end }}
    {{- end }}
  {{- end }}
  {{- if and (not (empty $otelcol_agent_data_source.metrics.prometheus.push)) (not (empty $otelcol_agent_data_source.metrics.prometheus.push.host)) }}
    {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "prometheusremotewrite" }}
  prometheusremotewrite:
    {{- if empty $otelcol_agent_data_source.metrics.prometheus.push.host }}
    endpoint: "{{ $otelcol_agent_data_source.metrics.prometheus.push.host }}"
    {{- else }}
    endpoint: "{{ $otelcol_agent_data_source.metrics.prometheus.push.host }}:{{ $otelcol_agent_data_source.metrics.prometheus.push.port | default "" }}"
    {{- end }}
    tls:
      insecure: true
    target_info:
      enabled: true
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_agent_data_source.logs) }}    
  {{- if not (empty $otelcol_agent_data_source.logs.otlp) }}
    {{- if not (empty $otelcol_agent_data_source.logs.otlp.grpc_endpoint) -}}
      {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "otlp/logs" }}
  otlp/logs:
    endpoint: "{{ $otelcol_agent_data_source.logs.otlp.grpc_endpoint }}"
    tls:
      insecure: "{{ $otelcol_agent_data_source.logs.otlp.grpc_insecure | default "true" }}"
      {{- if not (empty $otelcol_agent_data_source.logs.otlp.grpc_ca_file) }}
      ca_file: "{{ $otelcol_agent_data_source.logs.otlp.grpc_ca_file }}"
      {{- end }}
    timeout: "{{ $otelcol_agent_data_source.logs.otlp.grpc_timeout | default "10s" }}"
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.logs.otlp.http_endpoint) -}}
      {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "otlphttp/logs" }}
  otlphttp/logs:
    endpoint: "{{ $otelcol_agent_data_source.logs.otlp.http_endpoint }}"
    timeout: "{{ $otelcol_agent_data_source.logs.otlp.http_timeout | default "10s" }}"
    {{- end }}
  {{- end }}
{{- end }}
processors:
  batch:
    send_batch_size: 512
    send_batch_max_size: 2048
    timeout: 10s

connectors:
  spanmetrics:
    namespace: "{{ $otelcol_agent_data_source.additional_metrics_name | default "trace.metrics" }}"
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
      {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "file/rotation_trace" }}
      # exporters: [{{ $otelcol_traces_exporters | join ", "}}]
    metrics:
      receivers: [spanmetrics, otlp]
      processors: [batch]
      exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
      {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "file/rotation_metrics" }}
      # exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
    logs:
      receivers: [otlp]
      processors: [batch]
      exporters: [{{ $otelcol_logs_exporters | join ", "}}]
      {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "file/rotation_logs" }}
      # exporters: [{{ $otelcol_logs_exporters | join ", "}}]
