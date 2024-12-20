{{- with (dig "agent" "otelcol" "extensions" false .Values.telemetry) }}
extensions:
  {{- toYaml . | trim | nindent 2 }}
{{- end -}}

{{- /* ================== 准备上报分组 - begin ==================  */ -}}
{{- $otelcol_groups := list (dict "name" "default" "configure" .Values.telemetry.opentelemetry "agent" .Values.telemetry.agent) -}}
{{- $default_group_agent_shared_options := dig "agent" "shared_options" (dict) .Values.telemetry -}}
{{- range $group_name, $group_settings := .Values.telemetry.group }}
  {{- $current_group_agent_shared_options := dig "agent" "shared_options" (dict) $group_settings -}}
  {{- $current_group_agent_settings := merge ($group_settings.agent | default (dict)) (dict "shared_options" (merge $current_group_agent_shared_options $default_group_agent_shared_options) ) -}}
  {{- $otelcol_groups = append $otelcol_groups (dict "name" $group_name "configure" (omit $group_settings "agent") "agent" $current_group_agent_settings ) -}}
{{- end -}}
{{- /* ================== 准备上报分组 - end ==================  */ -}}

{{- /* ================== 生成 receivers 配置 ==================  */ -}}
{{- $otelcol_agent_settings_receivers := dict -}}
{{- range $group_settings := $otelcol_groups }}
  {{- /* 允许数据来自 <group>.agent.receivers */ -}}
  {{- if (dig "agent" "enable_trace" false $group_settings) -}}
    {{- $otelcol_agent_settings_receivers = merge $otelcol_agent_settings_receivers (dig "agent" "receivers" "trace" (dict) $group_settings) -}}
  {{- end }}
  {{- if (dig "agent" "enable_metrics" false $group_settings) -}}
    {{- $otelcol_agent_settings_receivers = merge $otelcol_agent_settings_receivers (dig "agent" "receivers" "metrics" (dict) $group_settings) -}}
  {{- end }}
  {{- if (dig "agent" "enable_logs" false $group_settings) -}}
    {{- $otelcol_agent_settings_receivers = merge $otelcol_agent_settings_receivers (dig "agent" "receivers" "logs" (dict) $group_settings) -}}
  {{- end }}
{{- end }}

receivers:
  {{- toYaml $otelcol_agent_settings_receivers | trim | nindent 2 }}

{{- /* ================== 生成 exporters 配置 ==================  */ -}}
{{- $otelcol_agent_settings_exporters := dict -}}
{{- range $group_settings := $otelcol_groups }}
  {{- $group_exporters_settings := merge (dict) (dig "agent" "otelcol" "exporters" "trace" (dict) $group_settings) -}}
  {{- $group_exporters_settings = merge $group_exporters_settings (dig "agent" "otelcol" "exporters" "metrics" (dict) $group_settings) -}}
  {{- $group_exporters_settings = merge $group_exporters_settings (dig "agent" "otelcol" "exporters" "logs" (dict) $group_settings) -}}
  {{- $group_exporters_settings = merge $group_exporters_settings (dig "agent" "otelcol" "exporters" "spanmetrics" (dict) $group_settings) -}}
  {{- /* Trace */ -}}
  {{- if and (dig "agent" "enable_trace" false $group_settings) (not (dig "agent" "trace_exporters" "trace_blackhole" false $group_settings)) -}}
    {{- /* OTLP gRPC exporter */ -}}
    {{- if (dig "configure" "trace" "otlp" "grpc" "endpoint" false $group_settings) -}}
      {{- $otlp_grpc_exporter := dict "endpoint" $group_settings.configure.trace.otlp.grpc.endpoint -}}
      {{- if (hasKey $group_settings.configure.trace.otlp.grpc "insecure") -}}
        {{- $otlp_grpc_exporter = merge $otlp_grpc_exporter (dict "tls" (dict "insecure" $group_settings.configure.trace.otlp.grpc.insecure)) -}}
      {{- end -}}
      {{- $otlp_grpc_exporter = merge $otlp_grpc_exporter (dig "agent" "shared_options" "exporters" "otlp" "grpc" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlp/%v_%v" "trace" $group_settings.name) $otlp_grpc_exporter) -}}
    {{- end }}
    {{- /* OTLP HTTP exporter */ -}}
    {{- if (dig "configure" "trace" "otlp" "http" "endpoint" false $group_settings) -}}
      {{- $otlp_http_exporter := dict "endpoint" $group_settings.configure.trace.otlp.http.endpoint -}}
      {{- if (hasKey $group_settings.configure.trace.otlp.http "insecure") -}}
        {{- $otlp_http_exporter = merge $otlp_http_exporter (dict "tls" (dict "insecure" $group_settings.configure.trace.otlp.http.insecure)) -}}
      {{- end -}}
      {{- $otlp_http_exporter = merge $otlp_http_exporter (dig "agent" "shared_options" "exporters" "otlp" "http" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlphttp/%v_%v" "trace" $group_settings.name) $otlp_http_exporter) -}}
    {{- end }}
    {{- /* File exporter */ -}}
    {{- if (dig "agent" "trace_exporters" "otlp" "file" "enable" false $group_settings) -}}
      {{- $file_exporter := dict "path" (printf "%v/otelcol-%v-%v.log" (dig "agent" "shared_options" "exporters" "file_dir" "../log" $group_settings) "trace" $group_settings.name) -}}
      {{- $file_exporter = merge $file_exporter (dig "agent" "shared_options" "exporters" "file" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "file/rotation_%v_%v" "trace" $group_settings.name) $file_exporter) -}}
    {{- end }}
  {{- end }}
  {{- /* Metrics */ -}}
  {{- if (dig "agent" "enable_metrics" false $group_settings) -}}
    {{- /* OTLP gRPC exporter */ -}}
    {{- if (dig "configure" "metrics" "otlp" "grpc" "endpoint" false $group_settings) -}}
      {{- $otlp_grpc_exporter := dict "endpoint" $group_settings.configure.metrics.otlp.grpc.endpoint -}}
      {{- if (hasKey $group_settings.configure.metrics.otlp.grpc "insecure") -}}
        {{- $otlp_grpc_exporter = merge $otlp_grpc_exporter (dict "tls" (dict "insecure" $group_settings.configure.metrics.otlp.grpc.insecure)) -}}
      {{- end -}}
      {{- $otlp_grpc_exporter = merge $otlp_grpc_exporter (dig "agent" "shared_options" "exporters" "otlp" "grpc" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlp/%v_%v" "metrics" $group_settings.name) $otlp_grpc_exporter) -}}

      {{- /* Spanmetrics exporter */ -}}
      {{- range $spanmetrics_name, $spanmetrics_settings := (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) }}
        {{- if (dig "enable" false $spanmetrics_settings) -}}
          {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlp/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) $otlp_grpc_exporter) -}}
        {{- end }}
      {{- end }}
    {{- end }}
    {{- /* OTLP HTTP exporter */ -}}
    {{- if (dig "configure" "metrics" "otlp" "http" "endpoint" false $group_settings) -}}
      {{- $otlp_http_exporter := dict "endpoint" $group_settings.configure.metrics.otlp.http.endpoint -}}
      {{- if (hasKey $group_settings.configure.metrics.otlp.http "insecure") -}}
        {{- $otlp_http_exporter = merge $otlp_http_exporter (dict "tls" (dict "insecure" $group_settings.configure.metrics.otlp.http.insecure)) -}}
      {{- end -}}
      {{- $otlp_http_exporter = merge $otlp_http_exporter (dig "agent" "shared_options" "exporters" "otlp" "http" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlphttp/%v_%v" "metrics" $group_settings.name) $otlp_http_exporter) -}}

      {{- /* Spanmetrics exporter */ -}}
      {{- range $spanmetrics_name, $spanmetrics_settings := (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) }}
        {{- if (dig "enable" false $spanmetrics_settings) -}}
          {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlphttp/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) $otlp_http_exporter) -}}
        {{- end }}
      {{- end }}
    {{- end }}
    {{- /* <group>.configure.metrics.prometheus.push 是gateway，不是remotewrite , 不能直接使用 */ -}}
    {{- /* Prometheus push exporter can be set by <group>.agent.otelcol.exporters.metrics.prometheusremotewrite/metrics_<name> */ -}}
    {{- /* Prometheus pull exporter can be set by <group>.agent.otelcol.exporters.metrics.prometheus/metrics_<name> */ -}}
    {{- /* File exporter */ -}}
    {{- if (dig "agent" "metrics_exporters" "otlp" "file" "enable" false $group_settings) -}}
      {{- $file_exporter_log_dir := (dig "agent" "shared_options" "exporters" "file_dir" "../log" $group_settings) -}}
      {{- $file_exporter := dict "path" (printf "%v/otelcol-%v-%v.log" $file_exporter_log_dir "metrics" $group_settings.name) -}}
      {{- $file_exporter = merge $file_exporter (dig "agent" "shared_options" "exporters" "file" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "file/rotation_%v_%v" "metrics" $group_settings.name) $file_exporter) -}}

      {{- /* Spanmetrics exporter */ -}}
      {{- range $spanmetrics_name, $spanmetrics_settings := (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) }}
        {{- if (dig "enable" false $spanmetrics_settings) -}}
          {{- $file_exporter = set $file_exporter "path" (printf "%v/otelcol-%v-%v-%v.log" $file_exporter_log_dir "spanmetrics" $group_settings.name $spanmetrics_name) -}}
          {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "file/rotation_%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) $file_exporter) -}}
        {{- end }}
      {{- end }}
    {{- end }}
  {{- end }}
  {{- /* Logs */ -}}
  {{- if (dig "agent" "enable_logs" false $group_settings) -}}
    {{- /* OTLP gRPC exporter */ -}}
    {{- if (dig "configure" "logs" "otlp" "grpc" "endpoint" false $group_settings) -}}
      {{- $otlp_grpc_exporter := dict "endpoint" $group_settings.configure.logs.otlp.grpc.endpoint -}}
      {{- if (hasKey $group_settings.configure.logs.otlp.grpc "insecure") -}}
        {{- $otlp_grpc_exporter = merge $otlp_grpc_exporter (dict "tls" (dict "insecure" $group_settings.configure.logs.otlp.grpc.insecure)) -}}
      {{- end -}}
      {{- $otlp_grpc_exporter = merge $otlp_grpc_exporter (dig "agent" "shared_options" "exporters" "otlp" "grpc" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlp/%v_%v" "logs" $group_settings.name) $otlp_grpc_exporter) -}}
    {{- end }}
    {{- /* OTLP HTTP exporter */ -}}
    {{- if (dig "configure" "logs" "otlp" "http" "endpoint" false $group_settings) -}}
      {{- $otlp_http_exporter := dict "endpoint" $group_settings.configure.logs.otlp.http.endpoint -}}
      {{- if (hasKey $group_settings.configure.logs.otlp.http "insecure") -}}
        {{- $otlp_http_exporter = merge $otlp_http_exporter (dict "tls" (dict "insecure" $group_settings.configure.logs.otlp.http.insecure)) -}}
      {{- end -}}
      {{- $otlp_http_exporter = merge $otlp_http_exporter (dig "agent" "shared_options" "exporters" "otlp" "http" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "otlphttp/%v_%v" "logs" $group_settings.name) $otlp_http_exporter) -}}
    {{- end }}
    {{- /* File exporter */ -}}
    {{- if (dig "agent" "logs_exporters" "otlp" "file" "enable" false $group_settings) -}}
      {{- $file_exporter := dict "path" (printf "%v/otelcol-%v-%v.log" (dig "agent" "shared_options" "exporters" "file_dir" "../log" $group_settings) "logs" $group_settings.name) -}}
      {{- $file_exporter = merge $file_exporter (dig "agent" "shared_options" "exporters" "file" (dict) $group_settings) -}}
      {{- $group_exporters_settings = merge $group_exporters_settings (dict (printf "file/rotation_%v_%v" "logs" $group_settings.name) $file_exporter) -}}
    {{- end }}
  {{- end }}

  {{- $otelcol_agent_settings_exporters = merge $otelcol_agent_settings_exporters $group_exporters_settings -}}
{{- end }}

exporters:
  debug:
    verbosity: normal # basic/normal/detailed
    sampling_initial: 5
    sampling_thereafter: 500
  {{- toYaml $otelcol_agent_settings_exporters | trim | nindent 2 }}

{{- /* ================== 生成 processors 配置 ==================  */ -}}
{{- $otelcol_agent_settings_processors := dict -}}
{{- range $group_settings := $otelcol_groups }}
  {{- if (dig "agent" "enable_trace" false $group_settings) -}}
    {{- $otelcol_agent_settings_processor_name := printf "batch/%v_%v" "trace" $group_settings.name -}}
    {{- $otelcol_agent_settings_processor_settings := merge (dict) (dig "agent" "shared_options" "processors" "batch" "trace" (dict) $group_settings) -}}
    {{- $otelcol_agent_settings_processor_settings = merge $otelcol_agent_settings_processor_settings (dig "agent" "shared_options" "processors" "batch" "default" (dict) $group_settings) -}}
    {{- $otelcol_agent_settings_processors = merge $otelcol_agent_settings_processors (dict $otelcol_agent_settings_processor_name $otelcol_agent_settings_processor_settings) -}}
  {{- end }}
  {{- if (dig "agent" "enable_metrics" false $group_settings) -}}
    {{- $otelcol_agent_settings_processor_name := printf "batch/%v_%v" "metrics" $group_settings.name -}}
    {{- $otelcol_agent_settings_processor_settings := merge (dict) (dig "agent" "shared_options" "processors" "batch" "metrics" (dict) $group_settings) -}}
    {{- $otelcol_agent_settings_processor_settings = merge $otelcol_agent_settings_processor_settings (dig "agent" "shared_options" "processors" "batch" "default" (dict) $group_settings) -}}
    {{- $otelcol_agent_settings_processors = merge $otelcol_agent_settings_processors (dict $otelcol_agent_settings_processor_name $otelcol_agent_settings_processor_settings) -}}
  {{- end }}
  {{- if (dig "agent" "enable_logs" false $group_settings) -}}
    {{- $otelcol_agent_settings_processor_name := printf "batch/%v_%v" "logs" $group_settings.name -}}
    {{- $otelcol_agent_settings_processor_settings := merge (dict) (dig "agent" "shared_options" "processors" "batch" "logs" (dict) $group_settings) -}}
    {{- $otelcol_agent_settings_processor_settings = merge $otelcol_agent_settings_processor_settings (dig "agent" "shared_options" "processors" "batch" "default" (dict) $group_settings) -}}
    {{- $otelcol_agent_settings_processors = merge $otelcol_agent_settings_processors (dict $otelcol_agent_settings_processor_name $otelcol_agent_settings_processor_settings) -}}
  {{- end }}
  {{- /* Spanmetrics */ -}}
  {{- range $spanmetrics_name, $spanmetrics_settings := (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) }}
    {{- if (dig "enable" false $spanmetrics_settings) -}}
      {{- $otelcol_agent_settings_processor_name := printf "batch/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name -}}
      {{- $otelcol_agent_settings_processor_settings := merge (dict) (dig "agent" "shared_options" "processors" "batch" "spanmetrics" (dict) $group_settings) -}}
      {{- $otelcol_agent_settings_processor_settings = merge $otelcol_agent_settings_processor_settings (dig "agent" "shared_options" "processors" "batch" "default" (dict) $group_settings) -}}
      {{- $otelcol_agent_settings_processors = merge $otelcol_agent_settings_processors (dict $otelcol_agent_settings_processor_name $otelcol_agent_settings_processor_settings) -}}

      {{- /* 自动设置和合并 resource 属性 */ -}}
      {{- $otelcol_agent_settings_resource_name := printf "resource/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name -}}
      {{- $otelcol_agent_settings_resource_attributes := concat (list) (dig "agent" "shared_options" "resource" "spanmetrics" $group_settings.name "attributes" (list) $group_settings) -}}
      {{- $otelcol_agent_settings_resource_custom := dict -}}
      {{- $otelcol_agent_settings_resource_from_metrics := merge (dict) (dig "configure" "metrics" "resource" (dict) $group_settings) -}}
      {{- $otelcol_agent_settings_resource_from_metrics = merge $otelcol_agent_settings_resource_from_metrics (dig "configure" "resource" (dict) $group_settings) -}}
      {{- range $attribute_action := $otelcol_agent_settings_resource_attributes -}}
        {{- if $attribute_action.key -}}
          {{- $otelcol_agent_settings_resource_custom = set $otelcol_agent_settings_resource_custom $attribute_action.key true -}}
        {{- end -}}
      {{- end }}
      {{- range $attribute_key,$attribute_value := $otelcol_agent_settings_resource_from_metrics -}}
        {{- if not (hasKey $otelcol_agent_settings_resource_custom $attribute_key ) -}}
          {{- $otelcol_agent_settings_resource_attributes = prepend $otelcol_agent_settings_resource_attributes (dict "action" "upsert" "key" $attribute_key "value" $attribute_value) -}}
        {{- end }}
      {{- end }}
      {{- $otelcol_agent_settings_processors = merge $otelcol_agent_settings_processors (dict $otelcol_agent_settings_resource_name (dict "attributes" $otelcol_agent_settings_resource_attributes)) -}}
    {{- end }}
  {{- end }}
{{- end }}

processors:
  {{- toYaml $otelcol_agent_settings_processors | trim | nindent 2 }}

{{- /* ================== 生成 connectors 配置 ==================  */ -}}
{{- $otelcol_agent_settings_connectors := dict -}}

{{- range $group_settings := $otelcol_groups }}
  {{- /* Spanmetrics */ -}}
  {{- range $spanmetrics_name, $spanmetrics_settings := (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) }}
    {{- if (dig "enable" false $spanmetrics_settings) -}}
      {{- $otelcol_agent_settings_spanmetrics_name := printf "spanmetrics/%v_%v" $group_settings.name $spanmetrics_name -}}
      {{- $otelcol_agent_settings_spanmetrics_settings := merge (dict) (dig "agent" "shared_options" "connectors" "spanmetrics" $spanmetrics_name (dict) $group_settings) -}}
      {{- $otelcol_agent_settings_spanmetrics_settings = merge $otelcol_agent_settings_spanmetrics_settings (dig "agent" "shared_options" "connectors" "spanmetrics" "default" (dict) $group_settings) -}}
      {{- $otelcol_agent_settings_spanmetrics_settings = merge $otelcol_agent_settings_spanmetrics_settings (dict "namespace" (dig "configure" "trace" "additional_metrics_name" "atframework_trace" $group_settings) ) -}}

      {{- $otelcol_agent_settings_connectors = merge $otelcol_agent_settings_connectors (dict $otelcol_agent_settings_spanmetrics_name $otelcol_agent_settings_spanmetrics_settings) -}}
    {{- end }}
  {{- end }}
{{- end }}

{{- if $otelcol_agent_settings_connectors }}
connectors:
  {{- toYaml $otelcol_agent_settings_connectors | trim | nindent 2 }}
{{- end }}

service:
{{- with (dig "agent" "otelcol" "service" "telemetry" false .Values.telemetry) }}
  telemetry:
    {{- toYaml . | trim | nindent 4 }}
{{- end }}
{{- with (dig "agent" "otelcol" "service" "extensions" false .Values.telemetry) }}
  extensions:
    {{- range $extension_idx, $extension_value := . }}
    - {{ $extension_value }}
    {{- end }}
{{- end }}
  pipelines:
{{- range $group_settings := $otelcol_groups }}
  {{- /* Trace */ -}}
  {{- if (dig "agent" "enable_trace" false $group_settings) -}}
    {{- /* Receivers */ -}}
    {{- $group_pipeline_trace_receivers := list -}}
    {{- range $receiver_name, $receiver_settings := (dig "agent" "receivers" "trace" (dict) $group_settings) }}
      {{- $group_pipeline_trace_receivers = append $group_pipeline_trace_receivers $receiver_name -}}
    {{- end }}
    {{- /* Exporters */ -}}
    {{- $group_pipeline_trace_exporters := list -}}
    {{- if (not (dig "agent" "trace_exporters" "trace_blackhole" false $group_settings)) -}}
      {{- if (dig "configure" "trace" "otlp" "grpc" "endpoint" false $group_settings) -}}
        {{- $group_pipeline_trace_exporters = append $group_pipeline_trace_exporters (printf "otlp/%v_%v" "trace" $group_settings.name) -}}
      {{- end }}
      {{- if (dig "configure" "trace" "otlp" "http" "endpoint" false $group_settings) -}}
        {{- $group_pipeline_trace_exporters = append $group_pipeline_trace_exporters (printf "otlphttp/%v_%v" "trace" $group_settings.name) -}}
      {{- end }}
      {{- if (dig "agent" "trace_exporters" "otlp" "file" "enable" false $group_settings) -}}
        {{- $group_pipeline_trace_exporters = append $group_pipeline_trace_exporters (printf "file/rotation_%v_%v" "trace" $group_settings.name) -}}
      {{- end }}
    {{- end }}
    {{- range $group_pipeline_metrics_custom_exporter,$_ := (dig "agent" "otelcol" "exporters" "trace" (dict) $group_settings) }}
      {{- $group_pipeline_metrics_exporters = append $group_pipeline_metrics_exporters $group_pipeline_metrics_custom_exporter -}}
    {{- end }}
    {{- /* Spanmetrics exporter */ -}}
    {{- range $spanmetrics_name, $spanmetrics_settings := (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) }}
      {{- if (dig "enable" false $spanmetrics_settings) -}}
        {{- $group_pipeline_trace_exporters = append $group_pipeline_trace_exporters (printf "spanmetrics/%v_%v" $group_settings.name $spanmetrics_name) -}}
      {{- end }}
    {{- end }}
    {{- if and $group_pipeline_trace_receivers $group_pipeline_trace_exporters }}
    traces/{{- $group_settings.name -}}:
      {{- $group_pipeline_trace_processor_name := printf "batch/%v_%v" "trace" $group_settings.name -}}
      {{- toYaml (dict "receivers" $group_pipeline_trace_receivers "processors" (list $group_pipeline_trace_processor_name) "exporters" $group_pipeline_trace_exporters ) | trim | nindent 6 }}
    {{- end }}
  {{- end }}

  {{- /* Metrics */ -}}
  {{- if (dig "agent" "enable_metrics" false $group_settings) -}}
    {{- /* Receivers */ -}}
    {{- $group_pipeline_metrics_receivers := list -}}
    {{- range $receiver_name, $receiver_settings := (dig "agent" "receivers" "metrics" (dict) $group_settings) }}
      {{- $group_pipeline_metrics_receivers = append $group_pipeline_metrics_receivers $receiver_name -}}
    {{- end }}
    {{- /* Exporters */ -}}
    {{- $group_pipeline_metrics_exporters := list -}}
    {{- if (dig "configure" "metrics" "otlp" "grpc" "endpoint" false $group_settings) -}}
      {{- $group_pipeline_metrics_exporters = append $group_pipeline_metrics_exporters (printf "otlp/%v_%v" "metrics" $group_settings.name) -}}
    {{- end }}
    {{- if (dig "configure" "metrics" "otlp" "http" "endpoint" false $group_settings) -}}
      {{- $group_pipeline_metrics_exporters = append $group_pipeline_metrics_exporters (printf "otlphttp/%v_%v" "metrics" $group_settings.name) -}}
    {{- end }}
    {{- if (dig "agent" "metrics_exporters" "otlp" "file" "enable" false $group_settings) -}}
      {{- $group_pipeline_metrics_exporters = append $group_pipeline_metrics_exporters (printf "file/rotation_%v_%v" "metrics" $group_settings.name) -}}
    {{- end }}
    {{- range $group_pipeline_metrics_custom_exporter,$_ := (dig "agent" "otelcol" "exporters" "metrics" (dict) $group_settings) }}
      {{- $group_pipeline_metrics_exporters = append $group_pipeline_metrics_exporters $group_pipeline_metrics_custom_exporter -}}
    {{- end }}
    {{- if and $group_pipeline_metrics_receivers $group_pipeline_metrics_exporters }}
    metrics/basic_{{- $group_settings.name -}}:
      {{- $group_pipeline_metrics_processor_name := printf "batch/%v_%v" "metrics" $group_settings.name -}}
      {{- toYaml (dict "receivers" $group_pipeline_metrics_receivers "processors" (list $group_pipeline_metrics_processor_name) "exporters" $group_pipeline_metrics_exporters ) | trim | nindent 6 }}
    {{- end }}
  {{- end }}

  {{- /* Logs */ -}}
  {{- if (dig "agent" "enable_logs" false $group_settings) -}}
    {{- /* Receivers */ -}}
    {{- $group_pipeline_logs_receivers := list -}}
    {{- range $receiver_name, $receiver_settings := (dig "agent" "receivers" "logs" (dict) $group_settings) }}
      {{- $group_pipeline_logs_receivers = append $group_pipeline_logs_receivers $receiver_name -}}
    {{- end }}
    {{- /* Exporters */ -}}
    {{- $group_pipeline_logs_exporters := list -}}
    {{- if (dig "configure" "logs" "otlp" "grpc" "endpoint" false $group_settings) -}}
      {{- $group_pipeline_logs_exporters = append $group_pipeline_logs_exporters (printf "otlp/%v_%v" "logs" $group_settings.name) -}}
    {{- end }}
    {{- if (dig "configure" "logs" "otlp" "http" "endpoint" false $group_settings) -}}
      {{- $group_pipeline_logs_exporters = append $group_pipeline_logs_exporters (printf "otlphttp/%v_%v" "logs" $group_settings.name) -}}
    {{- end }}
    {{- if (dig "agent" "logs_exporters" "otlp" "file" "enable" false $group_settings) -}}
      {{- $group_pipeline_logs_exporters = append $group_pipeline_logs_exporters (printf "file/rotation_%v_%v" "logs" $group_settings.name) -}}
    {{- end }}
    {{- range $group_pipeline_metrics_custom_exporter,$_ := (dig "agent" "otelcol" "exporters" "logs" (dict) $group_settings) }}
      {{- $group_pipeline_metrics_exporters = append $group_pipeline_metrics_exporters $group_pipeline_metrics_custom_exporter -}}
    {{- end }}
    {{- if and $group_pipeline_logs_receivers $group_pipeline_logs_exporters }}
    logs/{{- $group_settings.name -}}:
      {{- $group_pipeline_logs_processor_name := printf "batch/%v_%v" "logs" $group_settings.name -}}
      {{- toYaml (dict "receivers" $group_pipeline_logs_receivers "processors" (list $group_pipeline_logs_processor_name) "exporters" $group_pipeline_logs_exporters ) | trim | nindent 6 }}
    {{- end }}
  {{- end }}

  {{- /* Spanmetrics */ -}}
  {{- if and (dig "agent" "enable_trace" false $group_settings) (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) (dig "agent" "enable_metrics" false $group_settings) -}}
    {{- /* Spanmetrics exporter */ -}}
    {{- range $spanmetrics_name, $spanmetrics_settings := (dig "agent" "trace_exporters" "spanmetrics" (dict) $group_settings) }}
      {{- if (dig "enable" false $spanmetrics_settings) -}}
        {{- /* Receivers */ -}}
        {{- $group_pipeline_spanmetrics_receivers := list (printf "spanmetrics/%v_%v" $group_settings.name $spanmetrics_name) -}}
        {{- range $receiver_name, $receiver_settings := (dig "agent" "receivers" "spanmetrics" (dict) $group_settings) }}
          {{- $group_pipeline_spanmetrics_receivers = append $group_pipeline_spanmetrics_receivers $receiver_name -}}
        {{- end }}
        {{- /* Processors */ -}}
        {{- $group_pipeline_spanmetrics_processors := list (printf "resource/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) (printf "batch/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) -}}
        {{- /* Exporters */ -}}
        {{- $group_pipeline_spanmetrics_exporters := list -}}
        {{- if (dig "configure" "metrics" "otlp" "grpc" "endpoint" false $group_settings) -}}
          {{- $group_pipeline_spanmetrics_exporters = append $group_pipeline_spanmetrics_exporters (printf "otlp/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) -}}
        {{- end }}
        {{- if (dig "configure" "metrics" "otlp" "http" "endpoint" false $group_settings) -}}
          {{- $group_pipeline_spanmetrics_exporters = append $group_pipeline_spanmetrics_exporters (printf "otlphttp/%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) -}}
        {{- end }}
        {{- if (dig "agent" "metrics_exporters" "otlp" "file" "enable" false $group_settings) -}}
          {{- $group_pipeline_spanmetrics_exporters = append $group_pipeline_spanmetrics_exporters (printf "file/rotation_%v_%v_%v" "spanmetrics" $group_settings.name $spanmetrics_name) -}}
        {{- end }}
        {{- range $group_pipeline_metrics_custom_exporter,$_ := (dig "agent" "otelcol" "exporters" "spanmetrics" (dict) $group_settings) }}
          {{- $group_pipeline_spanmetrics_exporters = append $group_pipeline_spanmetrics_exporters $group_pipeline_metrics_custom_exporter -}}
        {{- end }}
        {{- if and $group_pipeline_spanmetrics_receivers $group_pipeline_spanmetrics_exporters }}
    metrics/spanmetrics_{{- $group_settings.name -}}_{{- $spanmetrics_name -}}:
          {{- toYaml (dict "receivers" $group_pipeline_spanmetrics_receivers "processors" $group_pipeline_spanmetrics_processors "exporters" $group_pipeline_spanmetrics_exporters ) | trim | nindent 6 }}
        {{- end }}
      {{- end }}
    {{- end }}
  {{- end }}
{{- end }}
