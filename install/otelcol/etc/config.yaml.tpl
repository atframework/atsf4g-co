{{- $otelcol_traces_exporters := list -}}
{{- $otelcol_spanmetrics_exporters_default := list -}}
{{- $otelcol_spanmetrics_exporters_prometheus := list -}}
{{- $otelcol_spanmetrics_service_default := dict -}}
{{- $otelcol_spanmetrics_service_prometheus := dict -}}
{{- $otelcol_metrics_exporters := list -}}
{{- $otelcol_logs_exporters := list -}}
{{- $otelcol_agent_data_source := dict -}}
{{- $otelcol_spanmetrics_processors_default := list -}}
{{- $otelcol_spanmetrics_processors_prometheus := list -}}
{{- if not (empty .Values.telemetry.agent.group_name) -}}
  {{- $otelcol_agent_data_source = get .Values.telemetry.group .Values.telemetry.agent.group_name -}}
  {{- if empty $otelcol_agent_data_source -}}
    {{- $otelcol_agent_data_source = .Values.telemetry.opentelemetry -}}
  {{- end -}}
{{- else -}}
  {{- $otelcol_agent_data_source = .Values.telemetry.opentelemetry -}}
{{- end -}}
{{- $otelcol_logs_cs_actor_exporters := list -}}
{{- $otelcol_agent_cs_actor_data_source := dict -}}
{{- $otelcol_agent_cs_actor_enable_file := false -}}
{{- if not (empty .Values.telemetry.group) -}}
  {{- if not (empty .Values.telemetry.group.cs_actor) -}}
    {{- $otelcol_agent_cs_actor_data_source = .Values.telemetry.group.cs_actor -}}
    {{- $otelcol_agent_cs_actor_enable_file = (dig "agent" "logs_exporters" "file" "enable" false $otelcol_agent_cs_actor_data_source) -}}
  {{- end -}}
{{- end -}}
{{- $otelcol_logs_notification_exporters := list -}}
{{- $otelcol_agent_notification_data_source := dict -}}
{{- $otelcol_agent_notification_enable_file := false -}}
{{- if not (empty .Values.telemetry.group) -}}
  {{- if not (empty .Values.telemetry.group.notification) -}}
    {{- $otelcol_agent_notification_data_source = .Values.telemetry.group.notification -}}
    {{- $otelcol_agent_notification_enable_file = (dig "agent" "logs_exporters" "file" "enable" false $otelcol_agent_notification_data_source) -}}
  {{- end -}}
{{- end -}}
{{- define "otelcol.config.otlp_agent.grpc.yaml" -}}
  {{- $otelcol_current_config := deepCopy .shared_options | default (dict ) -}}
  {{- if not (empty .private_options.grpc_endpoint) }}
    {{- $otelcol_current_config := set $otelcol_current_config "endpoint" .private_options.grpc_endpoint -}}
  {{- end }}
  {{- if not (empty .private_options.grpc_timeout) }}
    {{- $otelcol_current_config := set $otelcol_current_config "timeout" .private_options.grpc_timeout -}}
  {{- end }}
  {{- $otelcol_current_config := merge $otelcol_current_config (dict "tls" (dict "insecure" (.private_options.grpc_insecure | default true))) -}}
  {{- if not (empty .private_options.grpc_ca_file) }}
    {{- $otelcol_current_config := merge $otelcol_current_config (dict "tls" (dict "ca_file" .private_options.grpc_ca_file)) -}}
  {{- end }}
  {{- if not (empty .private_options.grpc_cert_file) }}
    {{- $otelcol_current_config := merge $otelcol_current_config (dict "tls" (dict "cert_file" .private_options.grpc_cert_file)) -}}
  {{- end }}
  {{- if not (empty .private_options.grpc_key_file) }}
    {{- $otelcol_current_config := merge $otelcol_current_config (dict "tls" (dict "key_file" .private_options.grpc_key_file)) -}}
  {{- end }}
  {{- toYaml $otelcol_current_config }}
{{- end -}}
{{- define "otelcol.config.otlp_agent.http.yaml" -}}
  {{- $otelcol_current_config := deepCopy .shared_options | default (dict ) -}}
  {{- if not (empty .private_options.http_endpoint) }}
    {{- $otelcol_current_config := set $otelcol_current_config "endpoint" .private_options.http_endpoint -}}
  {{- end }}
  {{- if not (empty .private_options.http_timeout) }}
    {{- $otelcol_current_config := set $otelcol_current_config "timeout" .private_options.http_timeout -}}
  {{- end }}
  {{- toYaml $otelcol_current_config }}
{{- end -}}
{{- define "otelcol.config.prometheus_agent.pull.yaml" -}}
  {{- $otelcol_current_config := deepCopy .shared_options | default (dict ) -}}
  {{- if empty (dig "endpoint" false $otelcol_current_config) }}
    {{- if not (empty .private_options.url) }}
      {{- $otelcol_current_config := set $otelcol_current_config "endpoint" .private_options.url -}}
    {{- end }}
  {{- end }}
  {{- if not (empty .private_options.tls) }}
    {{- $otelcol_current_config := merge $otelcol_current_config (dict "tls" .private_options.tls) -}}
  {{- end }}
  {{- if not (empty .private_options.namespace) }}
    {{- $otelcol_current_config := set $otelcol_current_config "namespace" .private_options.namespace -}}
  {{- end }}
  {{- if not (empty .private_options.resource_to_telemetry_conversion) }}
    {{- $otelcol_current_config := merge $otelcol_current_config (dict "resource_to_telemetry_conversion" .private_options.resource_to_telemetry_conversion) -}}
  {{- end }}
  {{- toYaml $otelcol_current_config }}
{{- end -}}
{{- define "otelcol.config.prometheus_agent.push.yaml" -}}
  {{- $otelcol_current_config := deepCopy .shared_options | default (dict ) -}}
  {{- if empty (dig "endpoint" false $otelcol_current_config) }}
    {{- if empty .private_options.port }}
      {{- $otelcol_current_config := set $otelcol_current_config "endpoint" .private_options.host -}}
    {{- else }}
      {{- $otelcol_current_config := set $otelcol_current_config "endpoint" (printf "%v:%v/api/v1/push" .private_options.host .private_options.port) -}}
    {{- end }}
  {{- end }}
  {{- if not (empty .private_options.namespace) }}
    {{- $otelcol_current_config := set $otelcol_current_config "namespace" .private_options.namespace -}}
  {{- end }}
  {{- if not (empty .private_options.resource_to_telemetry_conversion) }}
    {{- $otelcol_current_config := merge $otelcol_current_config (dict "resource_to_telemetry_conversion" .private_options.resource_to_telemetry_conversion) -}}
  {{- end }}
  {{- toYaml $otelcol_current_config }}
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
        endpoint: "127.0.0.1:4317"
  otlp/cs_actor:
    protocols:
      grpc:
        endpoint: "127.0.0.1:4327"
  otlp/notification:
    protocols:
      grpc:
        endpoint: "127.0.0.1:4337"

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
  file/rotation_spanmetrics:
    path: ../log/otelcol-spanmetrics.log
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
    path: ../log/otelcol-logs.log
    rotation:
      max_megabytes: 10
      max_days: 3
      max_backups: 10
      localtime: true
    format: json
    # compression: zstd
  file/rotation_logs_cs_actor:
    path: ../log/otelcol-cs_actor.log
    rotation:
      max_megabytes: 10
      max_days: 3
      max_backups: 10
      localtime: true
    format: json
    # compression: zstd
  file/rotation_logs_notification:
    path: ../log/otelcol-notification.log
    rotation:
      max_megabytes: 10
      max_days: 3
      max_backups: 10
      localtime: true
    format: json
    # compression: zstd

{{- if not (empty $otelcol_agent_data_source.trace) }}
  {{- if not (empty $otelcol_agent_data_source.trace.otlp) }}
    {{- if not (empty $otelcol_agent_data_source.trace.otlp.grpc_endpoint) -}}
      {{- if not (dig "trace_exporters" "trace_blackhole" false .Values.telemetry.agent) }}
        {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "otlp/trace" }}
      {{- end }}
  otlp/trace:
      {{- include "otelcol.config.otlp_agent.grpc.yaml" (dict "shared_options" (dig "shared_options" "otlp" "grpc" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.trace.otlp) | trim | nindent 4 }}
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.trace.otlp.http_endpoint) -}}
      {{- if not (dig "trace_exporters" "trace_blackhole" false .Values.telemetry.agent) }}
        {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "otlphttp/trace" }}
      {{- end }}
  otlphttp/trace:
      {{- include "otelcol.config.otlp_agent.http.yaml" (dict "shared_options" (dig "shared_options" "otlp" "http" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.trace.otlp) | trim | nindent 4 }}
    {{- end }}
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_agent_data_source.metrics) }}
  {{- if not (empty $otelcol_agent_data_source.metrics.otlp) }}
    {{- if not (empty $otelcol_agent_data_source.metrics.otlp.grpc_endpoint) -}}
      {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "otlp/metrics" }}
      {{- $otelcol_spanmetrics_exporters_default = append $otelcol_spanmetrics_exporters_default "otlp/spanmetrics" }}
  otlp/metrics:
      {{- include "otelcol.config.otlp_agent.grpc.yaml" (dict "shared_options" (dig "shared_options" "otlp" "grpc" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.metrics.otlp) | trim | nindent 4 }}
  otlp/spanmetrics:
      {{- include "otelcol.config.otlp_agent.grpc.yaml" (dict "shared_options" (dig "shared_options" "otlp" "grpc" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.metrics.otlp) | trim | nindent 4 }}
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.metrics.otlp.http_endpoint) -}}
      {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "otlphttp/metrics" }}
      {{- $otelcol_spanmetrics_exporters_default = append $otelcol_spanmetrics_exporters_default "otlphttp/spanmetrics" }}
  otlphttp/metrics:
      {{- include "otelcol.config.otlp_agent.http.yaml" (dict "shared_options" (dig "shared_options" "otlp" "http" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.metrics.otlp) | trim | nindent 4 }}
  otlphttp/spanmetrics:
      {{- include "otelcol.config.otlp_agent.http.yaml" (dict "shared_options" (dig "shared_options" "otlp" "http" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.metrics.otlp) | trim | nindent 4 }}
    {{- end }}
  {{- end }}
  {{- if or (dig "shared_options" "prometheus" "pull" "endpoint" false .Values.telemetry.agent) (dig "metrics" "prometheus" "pull" "url" false $otelcol_agent_data_source) }}
    {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "prometheus/metrics" }}
    {{- $otelcol_spanmetrics_exporters_prometheus = append $otelcol_spanmetrics_exporters_prometheus "prometheus/spanmetrics" }}
  prometheus/metrics:
    {{- include "otelcol.config.prometheus_agent.pull.yaml" (dict "shared_options" (dig "shared_options" "prometheus" "pull" (dict) .Values.telemetry.agent ) "private_options" (dig "metrics" "prometheus" "pull" (dict) $otelcol_agent_data_source)) | trim | nindent 4 }}
  prometheus/spanmetrics:
    {{- include "otelcol.config.prometheus_agent.pull.yaml" (dict "shared_options" (dig "shared_options" "prometheus" "pull" (dict) .Values.telemetry.agent ) "private_options" (dig "metrics" "prometheus" "pull" (dict) $otelcol_agent_data_source)) | trim | nindent 4 }}
  {{- end }}
  {{- if or (dig "shared_options" "prometheus" "push" "endpoint" false .Values.telemetry.agent) (dig "metrics" "prometheus" "push" "host" false $otelcol_agent_data_source) }}
    {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "prometheusremotewrite/metrics" }}
    {{- $otelcol_spanmetrics_exporters_prometheus = append $otelcol_spanmetrics_exporters_prometheus "prometheusremotewrite/spanmetrics" }}
  prometheusremotewrite/metrics:
    {{- include "otelcol.config.prometheus_agent.push.yaml" (dict "shared_options" (dig "shared_options" "prometheus" "push" (dict) .Values.telemetry.agent ) "private_options" (dig "metrics" "prometheus" "push" (dict) $otelcol_agent_data_source)) | trim | nindent 4 }}
  prometheusremotewrite/spanmetrics:
    {{- include "otelcol.config.prometheus_agent.push.yaml" (dict "shared_options" (dig "shared_options" "prometheus" "push" (dict) .Values.telemetry.agent ) "private_options" (dig "metrics" "prometheus" "push" (dict) $otelcol_agent_data_source)) | trim | nindent 4 }}
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_agent_data_source.logs) }}
  {{- if not (empty $otelcol_agent_data_source.logs.otlp) }}
    {{- if not (empty $otelcol_agent_data_source.logs.otlp.grpc_endpoint) -}}
      {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "otlp/logs" }}
  otlp/logs:
      {{- include "otelcol.config.otlp_agent.grpc.yaml" (dict "shared_options" (dig "shared_options" "otlp" "grpc" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.logs.otlp) | trim | nindent 4 }}
    {{- end }}
    {{- if not (empty $otelcol_agent_data_source.logs.otlp.http_endpoint) -}}
      {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "otlphttp/logs" }}
  otlphttp/logs:
      {{- include "otelcol.config.otlp_agent.http.yaml" (dict "shared_options" (dig "shared_options" "otlp" "http" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_data_source.logs.otlp) | trim | nindent 4 }}
    {{- end }}
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_agent_cs_actor_data_source.logs) }}
  {{- if not (empty $otelcol_agent_cs_actor_data_source.logs.otlp) }}
    {{- if not (empty $otelcol_agent_cs_actor_data_source.logs.otlp.grpc_endpoint) -}}
      {{- $otelcol_logs_cs_actor_exporters = append $otelcol_logs_cs_actor_exporters "otlp/logs_cs_actor" }}
  otlp/logs_cs_actor:
      {{- include "otelcol.config.otlp_agent.grpc.yaml" (dict "shared_options" (dig "shared_options" "otlp" "grpc" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_cs_actor_data_source.logs.otlp) | trim | nindent 4 }}
    {{- end }}
    {{- if not (empty $otelcol_agent_cs_actor_data_source.logs.otlp.http_endpoint) -}}
      {{- $otelcol_logs_cs_actor_exporters = append $otelcol_logs_cs_actor_exporters "otlphttp/logs_cs_actor" }}
  otlphttp/logs_cs_actor:
      {{- include "otelcol.config.otlp_agent.http.yaml" (dict "shared_options" (dig "shared_options" "otlp" "http" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_cs_actor_data_source.logs.otlp) | trim | nindent 4 }}
    {{- end }}
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_agent_notification_data_source.logs) }}
  {{- if not (empty $otelcol_agent_notification_data_source.logs.otlp) }}
    {{- if not (empty $otelcol_agent_notification_data_source.logs.otlp.grpc_endpoint) -}}
      {{- $otelcol_logs_notification_exporters = append $otelcol_logs_notification_exporters "otlp/logs_notification" }}
  otlp/logs_notification:
      {{- include "otelcol.config.otlp_agent.grpc.yaml" (dict "shared_options" (dig "shared_options" "otlp" "grpc" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_notification_data_source.logs.otlp) | trim | nindent 4 }}
    {{- end }}
    {{- if not (empty $otelcol_agent_notification_data_source.logs.otlp.http_endpoint) -}}
      {{- $otelcol_logs_notification_exporters = append $otelcol_logs_notification_exporters "otlphttp/logs_notification" }}
  otlphttp/logs_notification:
      {{- include "otelcol.config.otlp_agent.http.yaml" (dict "shared_options" (dig "shared_options" "otlp" "http" (dict) .Values.telemetry.agent ) "private_options" $otelcol_agent_notification_data_source.logs.otlp) | trim | nindent 4 }}
    {{- end }}
  {{- end }}
{{- end }}

processors:
  batch/traces:
    send_batch_size: 256
    send_batch_max_size: 16384
    timeout: 10s
{{- if not (empty (dig "otelcol" "processors" "batch" "metadata_keys" false .Values.telemetry.agent)) }}
    metadata_keys:
    {{- toYaml .Values.telemetry.agent.otelcol.processors.batch.metadata_keys | trim | nindent 6 }}
{{- end }}
  batch/spanmetrics_default:
    send_batch_size: 32
    send_batch_max_size: 256
    timeout: 10s
{{- if not (empty (dig "otelcol" "processors" "batch" "metadata_keys" false .Values.telemetry.agent)) }}
    metadata_keys:
    {{- toYaml .Values.telemetry.agent.otelcol.processors.batch.metadata_keys | trim | nindent 6 }}
{{- end }}
  batch/spanmetrics_prometheus:
    send_batch_size: 32
    send_batch_max_size: 256
    timeout: 10s
{{- if not (empty (dig "otelcol" "processors" "batch" "metadata_keys" false .Values.telemetry.agent)) }}
    metadata_keys:
    {{- toYaml .Values.telemetry.agent.otelcol.processors.batch.metadata_keys | trim | nindent 6 }}
{{- end }}
  batch/metrics:
    send_batch_size: 256
    send_batch_max_size: 4096
    timeout: 10s
{{- if not (empty (dig "otelcol" "processors" "batch" "metadata_keys" false .Values.telemetry.agent)) }}
    metadata_keys:
    {{- toYaml .Values.telemetry.agent.otelcol.processors.batch.metadata_keys | trim | nindent 6 }}
{{- end }}
  batch/logs:
    send_batch_size: 256
    send_batch_max_size: 8192
    timeout: 10s
{{- if not (empty (dig "otelcol" "processors" "batch" "metadata_keys" false .Values.telemetry.agent)) }}
    metadata_keys:
    {{- toYaml .Values.telemetry.agent.otelcol.processors.batch.metadata_keys | trim | nindent 6 }}
{{- end }}
  batch/logs_cs_actor:
    send_batch_size: 16
    send_batch_max_size: 8192
    timeout: 30s
{{- if not (empty (dig "otelcol" "processors" "batch" "metadata_keys" false .Values.telemetry.agent)) }}
    metadata_keys:
    {{- toYaml .Values.telemetry.agent.otelcol.processors.batch.metadata_keys | trim | nindent 4 }}
{{- end }}
  batch/logs_notification:
    send_batch_size: 16
    send_batch_max_size: 8192
    timeout: 30s
{{- if not (empty (dig "otelcol" "processors" "batch" "metadata_keys" false .Values.telemetry.agent)) }}
    metadata_keys:
    {{- toYaml .Values.telemetry.agent.otelcol.processors.batch.metadata_keys | trim | nindent 4 }}
{{- end }}
{{- $otelcol_agent_data_source_metrics_resource_attributes := dict -}}
{{- if not (empty $otelcol_agent_data_source.metrics) }}
  {{- if not (empty $otelcol_agent_data_source.metrics.resource) }}
    {{- $otelcol_agent_data_source_metrics_resource_attributes := merge $otelcol_agent_data_source_metrics_resource_attributes $otelcol_agent_data_source.metrics.resource -}}
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_agent_data_source.resource) }}
  {{- $otelcol_agent_data_source_metrics_resource_attributes := merge $otelcol_agent_data_source_metrics_resource_attributes $otelcol_agent_data_source.resource -}}
{{- end }}
{{- if not (empty $otelcol_agent_data_source_metrics_resource_attributes) }}
  {{- $otelcol_agent_spanmetrics_ignore_resources := dig "spanmetrics" "default" "ignore_resources" (list) .Values.telemetry.agent }}
  {{- $otelcol_agent_spanmetrics_default_resources_list := list }}
    {{- range $label_key, $label_value := $otelcol_agent_data_source_metrics_resource_attributes }}
      {{- if not (has $label_key $otelcol_agent_spanmetrics_ignore_resources) }}
        {{- $otelcol_agent_spanmetrics_default_resources_list = append $otelcol_agent_spanmetrics_default_resources_list (dict "key" $label_key "value" $label_value "action" "upsert") }}
      {{- end }}
    {{- end }}
    {{- if not (empty $otelcol_agent_spanmetrics_default_resources_list) }}
      {{- $otelcol_spanmetrics_processors_default = append $otelcol_spanmetrics_processors_default "resource/spanmetrics_default" }}
  resource/spanmetrics_default:
      {{- toYaml (dict "attributes" $otelcol_agent_spanmetrics_default_resources_list ) | trim | nindent 4}}
    {{- end }}
  {{- $otelcol_agent_spanmetrics_ignore_resources := dig "spanmetrics" "prometheus" "ignore_resources" (list) .Values.telemetry.agent }}
  {{- $otelcol_agent_spanmetrics_prometheus_resources_list := list }}
    {{- range $label_key, $label_value := $otelcol_agent_data_source_metrics_resource_attributes }}
      {{- if not (has $label_key $otelcol_agent_spanmetrics_ignore_resources) }}
        {{- $otelcol_agent_spanmetrics_prometheus_resources_list = append $otelcol_agent_spanmetrics_prometheus_resources_list (dict "key" $label_key "value" $label_value "action" "upsert") }}
      {{- end }}
    {{- end }}
    {{- if not (empty $otelcol_agent_spanmetrics_prometheus_resources_list) }}
      {{- $otelcol_spanmetrics_processors_prometheus = append $otelcol_spanmetrics_processors_prometheus "resource/spanmetrics_prometheus" }}
  resource/spanmetrics_prometheus:
      {{- toYaml (dict "attributes" $otelcol_agent_spanmetrics_prometheus_resources_list ) | trim | nindent 4}}
    {{- end }}
{{- end }}
{{- $otelcol_spanmetrics_processors_default = append $otelcol_spanmetrics_processors_default "batch/spanmetrics_default" }}
{{- $otelcol_spanmetrics_processors_prometheus = append $otelcol_spanmetrics_processors_prometheus "batch/spanmetrics_prometheus" }}

{{- if not (dig "spanmetrics" "blackhole" false .Values.telemetry.agent.trace_exporters) }}
  {{- if and (not (empty $otelcol_spanmetrics_exporters_default)) (not (dig "spanmetrics" "default" "blackhole" false .Values.telemetry.agent.trace_exporters)) }}
    {{- $otelcol_spanmetrics_service_default = dict "processors" $otelcol_spanmetrics_processors_default }}
    {{- if and (not (empty .Values.telemetry.agent.trace_exporters.file)) (not (empty .Values.telemetry.agent.trace_exporters.file.enable)) }}
      {{- $otelcol_spanmetrics_exporters_default = append $otelcol_spanmetrics_exporters_default "file/rotation_spanmetrics" }}
    {{- end }}
    {{- $otelcol_spanmetrics_service_default = set $otelcol_spanmetrics_service_default "exporters" $otelcol_spanmetrics_exporters_default }}
  {{- end }}
  {{- if and (not (empty $otelcol_spanmetrics_exporters_prometheus)) (not (dig "spanmetrics" "prometheus" "blackhole" false .Values.telemetry.agent.trace_exporters)) }}
    {{- $otelcol_spanmetrics_service_prometheus = dict "processors" $otelcol_spanmetrics_processors_prometheus "exporters" $otelcol_spanmetrics_exporters_prometheus }}
  {{- end }}
{{- end }}

{{- if or (not (empty $otelcol_spanmetrics_exporters_default)) (not (empty $otelcol_spanmetrics_service_prometheus)) }}
connectors:
  {{- if (not (empty $otelcol_spanmetrics_exporters_default)) }}
    {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "spanmetrics/default" }}
  spanmetrics/default:
    namespace: "{{ $otelcol_agent_data_source.additional_metrics_name | default "trace.metrics" }}"
    metrics_flush_interval: 45s
    histogram:
      explicit:
        buckets: [2ms, 8ms, 16ms, 64ms, 256ms, 1s, 8s]
    exclude_dimensions: ['service.identity', 'process.pid', 'bk.data.token']
    dimensions_cache_size: 4096
    resource_metrics_cache_size: 4096
    exemplars:
      enabled: true
    dimensions:
      - name: deployment.environment
        default: UNSET
      - name: host.name
        default: localhost
      - name: rpc.atrpc.result_code
        default: "0"
      - name: rpc.atrpc.response_code
        default: "0"
      - name: rpc.method
        default: UNKNOWN
      - name: rpc.service
        default: NONE
      - name: rpc.system
        default: NONE
      - name: service.area.zone_id
        default: "0"
      - name: service.instance.name
        default: UNKNOWN
      # - name: rpc.atrpc.kind
      # - name: span.name
      # - name: service.name
    aggregation_temporality: "AGGREGATION_TEMPORALITY_DELTA"
  {{- end }}
  {{- if (not (empty $otelcol_spanmetrics_service_prometheus)) }}
    {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "spanmetrics/prometheus" }}
  spanmetrics/prometheus:
    namespace: "{{ $otelcol_agent_data_source.additional_metrics_name | default "trace.metrics" }}"
    metrics_flush_interval: 45s
    histogram:
      explicit:
        buckets: [2ms, 8ms, 16ms, 64ms, 256ms, 1s, 8s]
    exclude_dimensions: ['service.identity', 'process.pid', 'bk.data.token']
    dimensions_cache_size: 4096
    resource_metrics_cache_size: 4096
    exemplars:
      enabled: true
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
        default: NONE
      - name: rpc.system
        default: NONE
      - name: service.area.zone_id
        default: 0
      - name: service.instance.name
        default: UNKNOWN
      # - name: rpc.atrpc.kind
      # - name: span.name
      # - name: service.name
    aggregation_temporality: "AGGREGATION_TEMPORALITY_CUMULATIVE"
  {{- end }}
{{- end }}

service:
{{- if and .Values.telemetry.agent.otelcol (not (empty .Values.telemetry.agent.otelcol.service.telemetry)) }}
  telemetry:
    {{- toYaml .Values.telemetry.agent.otelcol.service.telemetry | trim | nindent 4 }}
{{- end }}
  extensions: [health_check, pprof, zpages]
  pipelines:
{{- if not (dig "trace_exporters" "trace_blackhole" false .Values.telemetry.agent) }}
  {{- if dig "trace_exporters" "file" "enable" false .Values.telemetry.agent }}
    {{- $otelcol_traces_exporters = append $otelcol_traces_exporters "file/rotation_trace" }}
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_traces_exporters) }}
    traces:
      receivers: [otlp]
      processors: [batch/traces]
      exporters: [{{ $otelcol_traces_exporters | join ", "}}]
{{- end }}
{{- if or (not (empty $otelcol_spanmetrics_service_default)) (not (empty $otelcol_spanmetrics_service_prometheus)) }}
  {{- if not (empty $otelcol_spanmetrics_service_default) }}
    metrics/spanmetrics_default:
      receivers: [spanmetrics/default]
      {{- toYaml $otelcol_spanmetrics_service_default | trim | nindent 6 }}
  {{- end }}
  {{- if not (empty $otelcol_spanmetrics_service_prometheus) }}
    metrics/spanmetrics_prometheus:
      receivers: [spanmetrics/prometheus]
      {{- toYaml $otelcol_spanmetrics_service_prometheus | trim | nindent 6 }}
  {{- end }}
{{- end }}
    metrics:
      receivers: [otlp]
      processors: [batch/metrics]
{{- if and (not (empty .Values.telemetry.agent.metrics_exporters.file)) (not (empty .Values.telemetry.agent.metrics_exporters.file.enable)) }}
      # exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
      {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "file/rotation_metrics" }}
      exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
{{- else }}
      exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
      {{- $otelcol_metrics_exporters = append $otelcol_metrics_exporters "file/rotation_metrics" }}
      # exporters: [{{ $otelcol_metrics_exporters | join ", "}}]
{{- end }}
    logs:
      receivers: [otlp]
      processors: [batch/logs]
{{- if and (not (empty .Values.telemetry.agent.logs_exporters.file)) (not (empty .Values.telemetry.agent.logs_exporters.file.enable)) }}
      # exporters: [{{ $otelcol_logs_exporters | join ", "}}]
      {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "file/rotation_logs" }}
      exporters: [{{ $otelcol_logs_exporters | join ", "}}]
{{- else }}
      exporters: [{{ $otelcol_logs_exporters | join ", "}}]
      {{- $otelcol_logs_exporters = append $otelcol_logs_exporters "file/rotation_logs" }}
      # exporters: [{{ $otelcol_logs_exporters | join ", "}}]
{{- end }}
{{- if not (empty $otelcol_logs_cs_actor_exporters) }}
    logs/cs_actor:
      receivers: [otlp/cs_actor]
      processors: [batch/logs_cs_actor]
  {{- if $otelcol_agent_cs_actor_enable_file }}
      # exporters: [{{ $otelcol_logs_cs_actor_exporters | join ", "}}]
      {{- $otelcol_logs_cs_actor_exporters = append $otelcol_logs_cs_actor_exporters "file/rotation_logs_cs_actor" }}
      exporters: [{{ $otelcol_logs_cs_actor_exporters | join ", "}}]
  {{- else }}
      exporters: [{{ $otelcol_logs_cs_actor_exporters | join ", "}}]
      {{- $otelcol_logs_cs_actor_exporters = append $otelcol_logs_cs_actor_exporters "file/rotation_logs_cs_actor" }}
      # exporters: [{{ $otelcol_logs_cs_actor_exporters | join ", "}}]
  {{- end }}
{{- end }}
{{- if not (empty $otelcol_logs_notification_exporters) }}
    logs/notification:
      receivers: [otlp/notification]
      processors: [batch/logs_notification]
  {{- if $otelcol_agent_notification_enable_file }}
      # exporters: [{{ $otelcol_logs_notification_exporters | join ", "}}]
      {{- $otelcol_logs_notification_exporters = append $otelcol_logs_notification_exporters "file/rotation_logs_notification" }}
      exporters: [{{ $otelcol_logs_notification_exporters | join ", "}}]
  {{- else }}
      exporters: [{{ $otelcol_logs_notification_exporters | join ", "}}]
      {{- $otelcol_logs_notification_exporters = append $otelcol_logs_notification_exporters "file/rotation_logs_notification" }}
      # exporters: [{{ $otelcol_logs_notification_exporters | join ", "}}]
  {{- end }}
{{- end }}
