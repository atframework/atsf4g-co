{{- define "atapp.atbus.service.settings.yaml" -}}
  {{- $bus_addr := include "libapp.busAddr" . -}}
  {{- $proxy_port := include "libapp.atbus.calculateAtproxyPort" . -}}
  {{- $service_port := include "libapp.atbus.calculateServicePort" . -}}
listen:
  {{- if (dig .Values.atapp.atbus "configure" "topology" "rule" "allow_direct_connection" false) }}
  - "atcp://${ATAPP_EXTERNAL_IP:-::}:{{ $service_port }}"
  {{- else if (eq .Values.atdtool_running_platform "windows") }}
  - "pipe://.\\pipe\\{{ .Values.atapp.deployment.project_name }}\\{{ include "libapp.name" . }}_{{ $bus_addr }}.sock"
  {{- else }}
  - "unix:///run/atapp/{{ .Values.atapp.deployment.project_name }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.sock"
  {{- end }}
  {{- if (dig .Values.atapp.atbus "policy" "enable_local_proxy" false) }}
proxy: "atcp://${ATAPP_EXTERNAL_IP:-127.0.0.1}:{{ $proxy_port }}"
  {{- else if (dig .Values.atapp.atbus "policy" "remote_proxy" false) }}
proxy: "{{ .Values.atapp.atbus.policy.remote_proxy }}" # address of upstream node
  {{- else }}
# proxy: "not set" # address of upstream node
  {{- end }}
  {{- if (dig .Values.atapp.atbus "policy" "gateway" false) }}
gateways:
    {{- toYaml .Values.atapp.atbus.policy.gateway | trim | nindent 2 }}
  {{- end -}}
{{- end -}}

{{- define "atapp.yaml" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{- $uniq_id := .Values.uniq_id -}}
atapp:
  # =========== bus configure ===========
  id: {{ $uniq_id }}
  name: {{ .Values.type_name | default (include "libapp.name" .) }}_{{ $bus_addr }}
  world_id: {{ .Values.world_id }}
  zone_id: {{ .Values.zone_id }}
  type_id: {{ required ".Values.type_id who entry required!" .Values.type_id }} # server type id
  type_name: {{ .Values.type_name | default (include "libapp.name" .) }}         # server type name
  area:
    {{- include "atapp.default.metadata.yaml" . | nindent 4 }}
  remove_pidfile_after_exit: false     # keep pid file after exited
  {{- with (include "libapp.configure.hostname" .) }}
  hostname: "{{ . }}"   # hostname, any host should has a unique name. if empty, we wil try to use the mac address
  {{- end }}
  {{- $atbus_settings := mergeOverwrite (dict ) .Values.atapp.atbus.configure (include "atapp.atbus.service.settings.yaml" .) -}}
  bus:
    {{- toYaml $atbus_settings | trim | nindent 4  }}
  worker_pool:
    {{- toYaml .Values.atapp.worker_pool | trim | nindent 4  }}
  # =========== upper configures can not be reload ===========
  # =========== log configure ===========
  log:
    level: {{ .Values.log_level }}            # log active level(disable/disabled, fatal, error, warn/warning, info, notice, debug)
    category:
      - name: "default"
        index: 0
        prefix: "[%F %T.%f][%L](%k:%n): "
{{- if or (eq .Values.log_stacktrace_level "disable") (eq .Values.log_stacktrace_level "disabled") }}
        stacktrace:
          min: disable
          max: disable
{{- else }}
        stacktrace:
          min: {{ .Values.log_stacktrace_level }}
          max: fatal
{{- end }}
        sink:
          # default error log for file
          - type: file
            level:
              min: warning
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 20MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.normal.error.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.normal.error.log"
            auto_flush: error
            flush_interval: 1s    # flush log interval
          - type: file
            level:
              min: debug
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 20MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.normal.all.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.normal.all.log"
            auto_flush: error
            flush_interval: 1s    # flush log interval
      - name: redis
        index: 1
        prefix: "[%F %T.%f][%L](%k:%n): "
        stacktrace:
          min: disable
          max: disable
        sink:
          - type: file
            level:
              min: debug
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 10MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.redis.all.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.redis.all.log"
            auto_flush: error
            flush_interval: 1s        # flush log interval
          - type: file
            level:
              min: warning
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 10MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.redis.error.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.redis.error.log"
            auto_flush: error
            flush_interval: 1s        # flush log interval
      - name: db_inner
        index: 2
        prefix: "[%F %T.%f][%L](%k:%n): "
        stacktrace:
          min: disable
          max: disable
        sink:
          - type: file
            level:
              min: debug
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 10MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db_inner.all.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db_inner.all.log"
            auto_flush: error
            flush_interval: 1s    # flush log interval
          - type: file
            level:
              min: warning
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 10MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db_inner.error.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db_inner.error.log"
            auto_flush: error
            flush_interval: 1s        # flush log interval
  # =========== timer ===========
  timer:
    {{- toYaml .Values.atapp.timer | trim | nindent 4  }}
{{- end }}
