{{- define "atapp.logic.opentelemetry.yaml" -}}
  {{- if .opentelemetry.resource }}
resource:
    {{- toYaml .opentelemetry.resource | trim | nindent 2 }}
  {{- end }}
  {{- if .opentelemetry.trace }}
trace:
    {{- if .opentelemetry.trace.resource }}
  resource:
      {{- toYaml .opentelemetry.trace.resource | trim | nindent 4 }}
    {{- end }}
  default_name: "{{ .opentelemetry.prefix_name }}{{ .libapp_name }}"
  additional_metrics_name: "{{ dig "trace" "additional_metrics_name" "" .opentelemetry }}"
  enable_additional_metrics: "{{ dig "trace" "enable_additional_metrics" false .opentelemetry }}"
  exporters:
    {{- if (dig "otlp" "grpc" "endpoint" false .opentelemetry.trace) }}
    otlp_grpc:
      {{- toYaml .opentelemetry.trace.otlp.grpc | trim | nindent 6 }}
    {{- end }}
    {{- if (dig "otlp" "http" "endpoint" false .opentelemetry.trace) }}
    otlp_http:
      {{- toYaml .opentelemetry.trace.otlp.http | trim | nindent 6 }}
    {{- end }}
    otlp_file:
    {{- if (dig "otlp" "file" "endpoint" false .opentelemetry.trace) }}
      file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.%N.jsonl" # @stdout, @stderr or file pattern
      alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.jsonl"
      {{- toYaml (unset (unset .opentelemetry.trace.otlp.file "endpoint") "enable") | trim | nindent 6 }}
    {{- else }}
      # file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.%N.jsonl" # @stdout, @stderr or file pattern
      # alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.jsonl"
    {{- end }}
    {{- if (dig "processors" false .opentelemetry.trace) }}
  processors:
      {{- toYaml .opentelemetry.trace.processors | trim | nindent 4 }}
    {{- end }}
    {{- if (dig "samplers" false .opentelemetry.trace) }}
  samplers:
      {{- toYaml .opentelemetry.trace.samplers | nindent 4 }}
    {{- else }}
    always_on: true
    # trace_id_ratio: 0.25
    {{- end }}
  {{- end }}
  {{- if .opentelemetry.metrics }}
metrics:
    {{- if .opentelemetry.metrics.resource }}
  resource:
      {{- toYaml .opentelemetry.metrics.resource | trim | nindent 4 }}
    {{- end }}
  default_name: "{{ .opentelemetry.prefix_name }}default"
    {{- if .opentelemetry.metrics.reader }}
  reader:
      {{- toYaml .opentelemetry.metrics.reader | trim | nindent 4 }}
    {{- end }}
  exporters:
    {{- if (dig "otlp" "grpc" "endpoint" false .opentelemetry.metrics) }}
    otlp_grpc:
      {{- toYaml .opentelemetry.metrics.otlp.grpc | trim | nindent 6 }}
    {{- end }}
    {{- if (dig "otlp" "http" "endpoint" false .opentelemetry.metrics) }}
    otlp_http:
      {{- toYaml .opentelemetry.metrics.otlp.http | trim | nindent 6 }}
    {{- end }}
    otlp_file:
    {{- if (dig "otlp" "file" "endpoint" false .opentelemetry.metrics) }}
      file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.%N.jsonl" # @stdout, @stderr or file pattern
      alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.jsonl"
      {{- toYaml (unset (unset .opentelemetry.metrics.otlp.file "endpoint") "enable") | trim | nindent 6 }}
    {{- else }}
      # file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.%N.jsonl" # @stdout, @stderr or file pattern
      # alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.jsonl"
    {{- end }}
    {{- if (dig "prometheus" "pull" "url" false .opentelemetry.metrics) }}
    prometheus_pull:
      {{- toYaml .opentelemetry.metrics.prometheus.pull | trim | nindent 6 }}
    {{- end }}
    {{- if (dig "prometheus" "push" "host" false .opentelemetry.metrics) }}
    prometheus_push:
      {{- toYaml .opentelemetry.metrics.prometheus.push | trim | nindent 6 }}
    {{- end }}
    {{- if (dig "prometheus" "file" "file_suffix" false .opentelemetry.metrics) }}
    prometheus_file:
      file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.{{ .opentelemetry.metrics.prometheus.file.file_suffix }}.telemetry.%N.log"
      alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.{{ .opentelemetry.metrics.prometheus.file.file_suffix }}.telemetry.log"
      {{- toYaml (unset .opentelemetry.metrics.prometheus.file "file_suffix") | trim | nindent 6 }}
    {{- end }}
    {{- if (dig "prometheus" "http_api" "url" false .opentelemetry.metrics) }}
    prometheus_http_api:
      {{- toYaml .opentelemetry.metrics.prometheus.http_api | trim | nindent 6 }}
    {{- end }}
  {{- end }}
  {{- if .opentelemetry.logs }}
logs:
    {{- if .opentelemetry.logs.resource }}
  resource:
      {{- toYaml .opentelemetry.logs.resource | trim | nindent 4 }}
    {{- end }}
  default_name: "{{ .opentelemetry.prefix_name }}{{ .libapp_name }}"
  exporters:
    {{- if (dig "otlp" "grpc" "endpoint" false .opentelemetry.logs) }}
    otlp_grpc:
      {{- toYaml .opentelemetry.logs.otlp.grpc | trim | nindent 6 }}
    {{- end }}
    {{- if (dig "otlp" "http" "endpoint" false .opentelemetry.logs) }}
    otlp_http:
      {{- toYaml .opentelemetry.logs.otlp.http | trim | nindent 6 }}
    {{- end }}
    otlp_file:
    {{- if (dig "otlp" "file" "endpoint" false .opentelemetry.logs) }}
      file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.%N.jsonl" # @stdout, @stderr or file pattern
      alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.jsonl"
      {{- toYaml (unset (unset .opentelemetry.logs.otlp.file "endpoint") "enable") | trim | nindent 6 }}
    {{- else }}
      # file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.%N.jsonl" # @stdout, @stderr or file pattern
      # alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.jsonl"
    {{- end }}
    {{- if (dig "processors" false .opentelemetry.logs) }}
  processors:
      {{- toYaml .opentelemetry.logs.processors | trim | nindent 4 }}
    {{- end }}
  {{- end }}
{{- end }}
