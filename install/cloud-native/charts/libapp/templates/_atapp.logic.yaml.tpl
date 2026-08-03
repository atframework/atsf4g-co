{{- define "atapp.logic.yaml" -}}
# =========== logic configure ===========
logic:
  world_id: {{ .Values.world_id }} # world_id
  zone_id: {{ .Values.zone_id }} # zone_id
  logic_id: {{ include "libapp.logicID" . }} # svr_zone_id
  server:
    log_path: "{{ .Values.server_log_dir }}"
  excel:
    enable: true
    bindir: "../../resource/excel"
  user:
    enable_session_actor_log: {{ .Values.enable_session_actor_log }}
  operation_support_system:
    oss_cfg:
      enable: {{ .Values.enable_oss_log }}
      file: {{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ include "libapp.busAddr" . }}.oss.%N.log
      writing_alias: {{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ include "libapp.busAddr" . }}.oss.log
      rotate:
        number: 10
        size: 20MB
      flush_interval: 1s
    mon_cfg:
      enable: {{ .Values.enable_mon_log }}
      file: {{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ include "libapp.busAddr" . }}.mon.%N.log
      writing_alias: {{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ include "libapp.busAddr" . }}.mon.log
      rotate:
        number: 3
        size: 20MB
      flush_interval: 1s
  {{- if and .Values.redis .Values.redis.enable }}
  db:
  {{- if and .Values.redis.cluster_mode }}
    cluster:
      host:
    {{- range $_, $addr := .Values.redis.addrs }}
        - {{ $addr }}
    {{- end }}
  {{- else }}
    raw:
      host:
    {{- range $_, $addr := .Values.redis.addrs }}
        - {{ $addr }}
    {{- end }}
  {{- end }}
    password: {{ .Values.redis.password }}
    record_prefix: {{ .Values.redis.record_prefix }}
    random_prefix: {{ .Values.redis.random_prefix }}
    timer:
      retry: {{ .Values.redis.retry_duration }}
      timeout: {{ .Values.redis.timeout_duration }}
      proc: {{ .Values.redis.proc_duration }}
  {{- end -}}
  {{- if and .Values.cachesvr_shared .Values.cachesvr_shared.enable }}
  cache:
    {{- toYaml .Values.cachesvr_shared | trim | nindent 4 }}
  {{- end -}}
  {{- if and .Values.cs_session .Values.cs_session.enable }}
  session:
    {{- toYaml .Values.cs_session | trim | nindent 4 }}
  {{- end }}
  telemetry:
    executor:
      max_metric_record_per_loop: {{ dig "executor" "max_metric_record_per_loop" "1000" .Values.telemetry }}
    opentelemetry:
      app_log:
        level: debug
        category:
          name: opentelemetry
          prefix: "[Log %L][%F %T.%f][%k:%n]: "
          stacktrace:
            min: disable
            max: disable
          sink:
            - type: file
              level:
                min: debug
                max: fatal
              rotate:
                number: 3
                size: 10485760 # 10MB
              file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ include "libapp.busAddr" . }}.telemetry.%N.log"
              writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ include "libapp.busAddr" . }}.telemetry.log"
              auto_flush: error
              flush_interval: 1m
      {{- /*Merge k8s cluster name*/ -}}
      {{- $opentelemetry_current_setting := mergeOverwrite .Values.telemetry.opentelemetry (dict "metrics" (dict "resource" (dict "k8s.cluster.name" ( include "libapp.cluster" . )))) -}}
      {{- $opentelemetry_data := dict "opentelemetry" $opentelemetry_current_setting "libapp_name" ( include "libapp.name" . ) "bus_addr" ( include "libapp.busAddr" . ) "server_log_dir" .Values.server_log_dir -}}
      {{- include "atapp.logic.opentelemetry.yaml" $opentelemetry_data | trim | nindent 6 }}
{{- if not (empty .Values.telemetry.agent) }}
    agent:
      {{- $opentelemetry_agent_data := dict "agent" .Values.telemetry.agent "libapp_name" ( include "libapp.name" . ) "bus_addr" ( include "libapp.busAddr" . ) "server_log_dir" .Values.server_log_dir -}}
      {{- include "atapp.logic.opentelemetry.agent.yaml" $opentelemetry_agent_data | trim | nindent 6 }}
{{- end }}
{{- if not (empty .Values.telemetry.group) }}
  {{- $opentelemetry_libapp_name := include "libapp.name" . }}
  {{- $opentelemetry_libapp_busaddr := include "libapp.busAddr" . }}
  {{- $opentelemetry_server_log_dir := .Values.server_log_dir }}
    group:
  {{- range $telemetry_group_name, $telemetry_group_settings := .Values.telemetry.group }}
      - name: "{{ $telemetry_group_name }}"
        configure:
          {{- /*Merge k8s cluster name*/ -}}
          {{- $opentelemetry_current_setting := mergeOverwrite $telemetry_group_settings (dict "metrics" (dict "resource" (dict "k8s.cluster.name" ( include "libapp.cluster" $ )))) -}}
          {{- $opentelemetry_data := dict "opentelemetry" $opentelemetry_current_setting "libapp_name" $opentelemetry_libapp_name "bus_addr" $opentelemetry_libapp_busaddr "server_log_dir" $opentelemetry_server_log_dir -}}
          {{- include "atapp.logic.opentelemetry.yaml" $opentelemetry_data  | trim | nindent 10 }}
    {{- if not (empty (get $opentelemetry_current_setting "agent")) }}
        agent:
          {{- $opentelemetry_agent_data := dict "agent" (get $opentelemetry_current_setting "agent") "libapp_name" $opentelemetry_libapp_name "bus_addr" $opentelemetry_libapp_busaddr "server_log_dir" $opentelemetry_server_log_dir -}}
          {{- include "atapp.logic.opentelemetry.agent.yaml" $opentelemetry_agent_data | trim | nindent 10 }}
    {{- end }}
  {{- end }}
{{- end }}
{{- if not (empty .Values.telemetry.osslog) }}
    osslog:
      task_oss_stats: {{ .Values.telemetry.osslog.task_oss_stats | default "false" }}
      populate_as_json_body: {{ .Values.telemetry.osslog.populate_as_json_body | default "false" }}
{{- end }}
  {{- /*Merge k8s cluster name*/ -}}
  {{- $hpa_current_setting := mergeOverwrite .Values.hpa (dict "metrics" (dict "labels" (dict "k8s.cluster.name" ( include "libapp.cluster" . )))) -}}
  {{- $hpa_data := dict "hpa" $hpa_current_setting  "autoscaling" (.Values.autoscaling | default dict) "libapp_name" ( include "libapp.name" . ) "libapp_type_id" .Values.type_id "libapp_hpa_target_name" ( include "libapp.hpa_target_name" . ) "chart" .Chart -}}
  {{- include "atapp.logic.hpa.yaml" $hpa_data | trim | nindent 2 }}
{{- end }}
