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
    cluster:
      host:
    {{- range $_, $addr := .Values.redis.addrs }}
        - {{ $addr }}
    {{- end }}
    password: {{ .Values.redis.password }}
    record_prefix: {{ .Values.redis.record_prefix }}
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
  {{- end -}}
{{- end }}
