
{{- define "atapp.atbus.service.settings.yaml" -}}
  {{- $bus_addr := include "libapp.busAddr" . -}}
  {{- $proxy_port := include "libapp.atbus.calculateAtproxyPort" . -}}
  {{- $service_port := include "libapp.atbus.calculateServicePort" . -}}
  {{- $atapp_external_ip := include "libapp.atappExternalIP" . -}}
listen:
  {{- if or (dig "configure" "topology" "rule" "allow_direct_connection" false .Values.atapp.atbus ) ( eq .Values.type_name "atproxy" ) }}
  - "atcp://{{ $atapp_external_ip }}:{{ $service_port }}"
  {{- else if (eq .Values.atdtool_running_platform "windows") }}
  - "pipe://\\\\.\\pipe\\{{ .Values.atapp.deployment.project_name }}\\{{ include "libapp.name" . }}_{{ $bus_addr }}.sock"
  {{- else }}
  - "unix:///tmp/atapp/{{ .Values.atapp.deployment.project_name }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.sock"
  {{- end }}
  {{- if and (dig "policy" "enable_local_proxy" false .Values.atapp.atbus ) ( ne .Values.type_name "atproxy" ) }}
proxy: "atcp://{{ $atapp_external_ip }}:{{ $proxy_port }}"
  {{- else if (dig "policy" "remote_proxy" false .Values.atapp.atbus ) }}
proxy: "{{ .Values.atapp.atbus.policy.remote_proxy }}" # address of upstream node
  {{- else }}
# proxy: "not set" # address of upstream node
  {{- end }}
  {{- if (dig "policy" "gateway" false .Values.atapp.atbus ) }}
gateways:
    {{- toYaml .Values.atapp.atbus.policy.gateway | trim | nindent 2 }}
  {{- end -}}
{{- end -}}

{{- define "atapp.yaml" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
atapp:
  # =========== bus configure ===========
  id: {{ $bus_addr }}
  id_mask: {{ .Values.id_mask }}
  name: {{ .Values.type_name | default (include "libapp.name" .) }}_{{ $bus_addr }}
  type_id: {{ required ".Values.type_id who entry required!" .Values.type_id }} # server type id
  type_name: {{ .Values.type_name | default (include "libapp.name" .) }}         # server type name
  area:
    zone_id: {{ .Values.zone_id }}
  metadata:
    {{- include "atapp.default.metadata.yaml" . | nindent 4 }}
  remove_pidfile_after_exit: false     # keep pid file after exited
  {{- with (include "libapp.configure.hostname" .) }}
  hostname: "{{ . }}"   # hostname, any host should has a unique name. if empty, we wil try to use the mac address
  {{- end }}
  {{- $atbus_settings := mergeOverwrite (dict ) .Values.atapp.atbus.configure (include "atapp.atbus.service.settings.yaml" . | fromYaml) }}
  bus:
    {{- toYaml $atbus_settings | trim | nindent 4  }}
  worker_pool:
    {{- toYaml .Values.atapp.worker_pool | trim | nindent 4  }}
  # =========== upper configures can not be reload ===========
  debug:
    windows_minidump_path: {{ .Values.server_log_dir }}
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
      - name: db
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
              size: 30MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db.all.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db.all.log"
            auto_flush: error
            flush_interval: 1s        # flush log interval
          - type: file
            level:
              min: warning
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 30MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db.error.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.db.error.log"
            auto_flush: error
            flush_interval: 1s        # flush log interval
      - name: stat
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
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.stat.all.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.stat.all.log"
            auto_flush: error
            flush_interval: 1s    # flush log interval
          - type: file
            level:
              min: warning
              max: fatal
            rotate:
              number: {{ .Values.log_rotate_num }}
              size: 10MB
            file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.stat.error.%N.log"
            writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.stat.error.log"
            auto_flush: error
            flush_interval: 1s        # flush log interval
      - name: db_inner
        index: 4
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

  # =========== etcd service for discovery ===========
  etcd:
    {{- if .Values.etcd }}
    enable: {{ .Values.etcd.enabled }}
    {{- else }}
    enable: false
    {{- end }}
{{- /* etcd module enabled */}}
{{- if and .Values.etcd .Values.etcd.enabled }}
{{- if .Values.etcd.log.enable }}
    log:
      startup_level: {{ .Values.etcd.log.startup_level }}
      level: {{ .Values.etcd.log.level }}
      category:
        - name: "etcd_default"
          prefix: "[Log %L][%F %T.%f][%s:%n(%C)]: " # log categorize 0's name = etcd_default
          stacktrace:
            min: disable
            max: disable
          sink:
            - type: file
              level:
                min: trace
                max: fatal
              rotate:
                number: 10
                size: 10MB
              file: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.etcd.%N.log"
              writing_alias: "{{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.etcd.log"
              auto_flush: info
              flush_interval: 1s # 1s (unit: s,m,h,d)
{{- end -}} {{- /* end if */}}
{{- if .Values.etcd.client_urls}}
    hosts:
{{- range $_, $host := .Values.etcd.client_urls }}
      - {{ $host }}
{{- end }} {{- /* end range client_urls */ -}}
{{- else }}
{{- $node_url_list := list -}}
{{- range $idx, $node := .Values.etcd_deploy_nodes }}
{{- $port := add $.etcd.etcd_listen_client_base_port $node.Index }}
{{- $url := printf "http://%s:%d" $node.InnerIP $port }}
      - {{ $url }}
{{- end }} {{- /* end range etcd_deploy_nodes */ -}}
{{- end -}} {{- /* end if */}}
{{- if .Values.partition }}
    path: {{ .Values.etcd.path }}/{{ include "libapp.environment" . }}/{{ .Values.partition }}
{{- else }}
    path: {{ .Values.etcd.path }}
{{- end }}
{{- if empty .Values.etcd.authorization }}
    # authorization:  # username:password
{{- else }}
    authorization: "{{ .Values.etcd.authorization }}"
{{- end }}
{{- if .Values.etcd.ssl }}
    ssl:
      enable_alpn: {{ .Values.etcd.ssl.enable_alpn }}
      verify_peer: {{ .Values.etcd.ssl.verify_peer }}
      ssl_min_version: {{ .Values.etcd.ssl.ssl_min_version }}
      ssl_client_cert: "../../etcd/ssl/{{ .Values.etcd.ssl.ssl_client_cert_file }}"
      ssl_client_key: "../../etcd/ssl/{{ .Values.etcd.ssl.ssl_client_key_file }}"
{{- if empty .Values.etcd.ssl.ssl_client_key_passwd }}
      # ssl_client_key_passwd:
{{- else }}
      ssl_client_key_passwd: {{ .Values.etcd.ssl.ssl_client_key_passwd }}
{{- end }}
      ssl_ca_cert: ../../etcd/ssl/{{ .Values.etcd.ssl.ssl_ca_cert_file }}
{{- if empty .Values.etcd.ssl.ssl_cipher_list }}
      # ssl_cipher_list:
{{- else }}
      ssl_cipher_list: "{{ .Values.etcd.ssl.ssl_cipher_list }}"
{{- end }}
{{- if empty .Values.etcd.ssl.ssl_cipher_list_tls13 }}
      # ssl_cipher_list_tls13:
{{- else }}
      ssl_cipher_list_tls13: "{{ .Values.etcd.ssl.ssl_cipher_list_tls13 }}"
{{- end }}
{{- end }} {{- /* end if .Values.etcd.ssl */}}
{{- if .Values.etcd.cluster }}
    cluster:
      auto_update: {{ .Values.etcd.cluster.auto_update }}       # set false when etcd service is behind a safe cluster(Kubernetes etc.)
      update_interval: {{ .Values.etcd.cluster.update_interval }}       # update etcd cluster members interval
      retry_interval: {{ .Values.etcd.cluster.retry_interval }}       # update etcd cluster retry interval
{{- end }} {{- /* end if .Values.etcd.cluster */}}
{{- if .Values.etcd.keepalive }}
    keepalive:
      timeout: {{ .Values.etcd.keepalive.timeout }}            # expired timeout
      ttl: {{ .Values.etcd.keepalive.ttl }}                # renew ttl interval
      retry_interval: {{ .Values.etcd.keepalive.retry_interval }} # keepalive retry interval
{{- end }} {{- /* end if .Values.etcd.keepalive */}}
{{- if .Values.etcd.request }}
    request:
      timeout: {{ .Values.etcd.request.timeout }}             # timeout for etcd request
      initialization_timeout: {{ .Values.etcd.request.initialization_timeout }} # timeout for etcd request when initializing
      connect_timeout: {{ .Values.etcd.request.connect_timeout }} # timeout for etcd request connect
      dns_cache_timeout: {{ .Values.etcd.request.dns_cache_timeout }} # timeout for dns cache of etcd request
      dns_servers: "{{ .Values.etcd.request.dns_servers }}" # dns servers: 8.8.8.8:53,1.1.1.1
{{- end }} {{- /* end if .Values.etcd.request */}}
{{- if .Values.etcd.init }}
    init:
      timeout: {{ .Values.etcd.init.timeout }}                  # initialize timeout
      tick_interval: 256ms
{{- end }} {{- /* end if .Values.etcd.init */}}
{{- if .Values.etcd.watcher }}
    watcher:
      retry_interval: {{ .Values.etcd.watcher.retry_interval }}       # retry interval watch when previous request failed
      request_timeout: {{ .Values.etcd.watcher.request_timeout }}       # request timeout for watching
      get_request_timeout: {{ .Values.etcd.watcher.get_request_timeout }}            # range request timeout for watcher
      startup_random_delay_min: {{ .Values.etcd.watcher.startup_random_delay_min }}  # delay start watching - min
      startup_random_delay_max: {{ .Values.etcd.watcher.startup_random_delay_max }}  # delay start watching - max
      by_id: {{ .Values.etcd.watcher.by_id }}       # watch service discovery by id
      by_name: {{ .Values.etcd.watcher.by_name }}       # watch service discovery by name
      # by_type_id: []
      # by_type_name: []
      # by_tag: []
{{- end }} {{- /* end if .Values.etcd.watcher */}}
{{- end }} {{- /* end if .Values.etcd */}}

{{- end }}
