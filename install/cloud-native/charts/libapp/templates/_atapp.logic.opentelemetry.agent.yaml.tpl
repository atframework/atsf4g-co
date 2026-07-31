{{- define "atapp.logic.opentelemetry.agent.yaml" -}}
  {{- if .agent.enable_trace }}
enable_trace: {{ .agent.enable_trace }}
  {{- end }}
  {{- if .agent.trace_exporters }}
trace_exporters:
    {{- if (dig "otlp" "grpc" "endpoint" false .agent.trace_exporters) }}
  otlp_grpc:
      {{- toYaml .agent.trace_exporters.otlp.grpc | trim | nindent 4 }}
    {{- end }}
    {{- if (dig "otlp" "http" "endpoint" false .agent.trace_exporters) }}
  otlp_http:
      {{- toYaml .agent.trace_exporters.otlp.http | trim | nindent 4 }}
    {{- end }}
  otlp_file:
    {{- if (dig "otlp" "file" "endpoint" false .agent.trace_exporters) }}
    file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.%N.jsonl" # @stdout, @stderr or file pattern
    alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.jsonl"
      {{- toYaml (unset (unset .agent.trace_exporters.otlp.file "endpoint") "enable") | trim | nindent 4 }}
    {{- else }}
    # file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.%N.jsonl" # @stdout, @stderr or file pattern
    # alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.trace.jsonl"
    {{- end }}
  {{- end }}
  {{- if .agent.enable_metrics }}
enable_metrics: {{ .agent.enable_metrics }}
  {{- end }}
  {{- if .agent.metrics_exporters }}
metrics_exporters:
    {{- if (dig "otlp" "grpc" "endpoint" false .agent.metrics_exporters) }}
  otlp_grpc:
      {{- toYaml .agent.metrics_exporters.otlp.grpc | trim | nindent 4 }}
    {{- end }}
    {{- if (dig "otlp" "http" "endpoint" false .agent.metrics_exporters) }}
  otlp_http:
      {{- toYaml .agent.metrics_exporters.otlp.http | trim | nindent 4 }}
    {{- end }}
  otlp_file:
    {{- if (dig "otlp" "file" "endpoint" false .agent.metrics_exporters) }}
    file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.%N.jsonl" # @stdout, @stderr or file pattern
    alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.jsonl"
      {{- toYaml (unset (unset .agent.metrics_exporters.otlp.file "endpoint") "enable") | trim | nindent 4 }}
    {{- else }}
    # file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.%N.jsonl" # @stdout, @stderr or file pattern
    # alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.metrics.jsonl"
    {{- end }}
    {{- if (dig "prometheus" "pull" "url" false .agent.metrics_exporters) }}
  prometheus_pull:
      {{- toYaml .agent.metrics_exporters.prometheus.pull | trim | nindent 4 }}
    {{- end }}
    {{- if (dig "prometheus" "push" "host" false .agent.metrics_exporters) }}
  prometheus_push:
      {{- toYaml .agent.metrics_exporters.prometheus.push | trim | nindent 4 }}
    {{- end }}
    {{- if (dig "prometheus" "file" "file_suffix" false .agent.metrics_exporters) }}
  prometheus_file:
    file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.{{ .agent.metrics_exporters.prometheus.file.file_suffix }}.telemetry.%N.log"
    alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.{{ .agent.metrics_exporters.prometheus.file.file_suffix }}.telemetry.log"
      {{- toYaml (unset .agent.metrics_exporters.prometheus.file "file_suffix") | trim | nindent 4 }}
    {{- end }}
  {{- end }}
  {{- if .agent.enable_logs }}
enable_logs: {{ .agent.enable_logs }}
  {{- end }}
  {{- if .agent.logs_exporters }}
logs_exporters:
    {{- if (dig "otlp" "grpc" "endpoint" false .agent.logs_exporters) }}
  otlp_grpc:
      {{- toYaml .agent.logs_exporters.otlp.grpc | trim | nindent 4 }}
    {{- end }}
    {{- if (dig "otlp" "http" "endpoint" false .agent.logs_exporters) }}
  otlp_http:
      {{- toYaml .agent.logs_exporters.otlp.http | trim | nindent 4 }}
    {{- end }}
  otlp_file:
    {{- if (dig "otlp" "file" "endpoint" false .agent.logs_exporters) }}
    file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.%N.jsonl" # @stdout, @stderr or file pattern
    alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.jsonl"
    {{- else }}
    # file_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.%N.jsonl" # @stdout, @stderr or file pattern
    # alias_pattern: "{{ .server_log_dir }}/{{ .libapp_name }}_{{ .bus_addr }}.logs.jsonl"
      {{- toYaml (unset (unset .agent.logs_exporters.otlp.file "endpoint") "enable") | trim | nindent 4 }}
    {{- end }}
  {{- end }}
{{- end }}
