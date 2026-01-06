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
{{ include "atapp.default.metadata.yaml" . | indent 4 }}
  remove_pidfile_after_exit: false     # keep pid file after exited
  {{- with .Values.inner_ip }}
  hostname: "{{ . }}"   # hostname, any host should has a unique name. if empty, we wil try to use the mac address
  {{- end }}
  bus:
    listen: unix:///run/atapp/{{ .Values.atapp.deployment.project_name }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.sock
    # bus.subnets: 0/0
    # proxy:                           # atgateway must has parent node
    loop_times: {{ .Values.atapp.bus_loop_times_per_tick | default 2048 }}                    # max message number in one loop
    ttl: {{ .Values.atapp.bus_ttl | default 16 }}                            # max ttl when transfer messages
    backlog: {{ .Values.atapp.backlog | default 256 }}                       # tcp backlog
    overwrite_listen_path: false       # overwrite the existing unix socket
    first_idle_timeout: 30s            # first idle timeout when have new connection(second)
    ping_interval: 8s                  # ping interval(second)
    retry_interval: 3s                 # retry interval when error happen(second)
    fault_tolerant: 2                  # how many errors at most to ignore, or it will kill the connection
    message_size: 256KB                    # max message size(256KB)
    recv_buffer_size: {{ default "2MB" .Values.atapp.bus_recv_buff_size }} # recv channel size(2MB), will be used to initialize (shared) memory channel size
    send_buffer_size: {{ default "1MB" .Values.atapp.bus_send_buff_size }} # send buffer size, will be used to initialize io_stream channel write queue
    send_buffer_number: 0              # send message number limit, will be used to initialize io_stream channel write queue, 0 for dynamic buffer
    gateways:
      address: ws://{{ include "libapp.name" . }}_{{ $bus_addr }}
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
    tick_interval: 8ms               # 8ms for tick active
    tick_round_timeout: 128ms
    stop_timeout: 30s                # 20s for stop operation
    stop_interval: 256ms
    initialize_timeout: 30s          # 20s for initialization
{{- end }}