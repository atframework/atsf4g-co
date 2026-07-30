
{{- define "atapp.atbus.service.settings.yaml" -}}
  {{- $bus_addr := include "libapp.busAddr" . -}}
  {{- $proxy_port := include "libapp.atbus.calculateAtproxyPort" . -}}
  {{- $service_port := include "libapp.atbus.calculateServicePort" . -}}
  {{- $atapp_external_ip := include "libapp.atappExternalIP" . -}}
listen:
  {{- if or (dig "configure" "topology" "rule" "allow_direct_connection" false .Values.atapp.atbus ) ( eq .Values.type_name "atproxy" ) }}
  - "atcp://{{ $atapp_external_ip }}:{{ $service_port }}"
  {{- else if (eq .Values.atdtool_running_platform "windows") }}
  - "atcp://{{ $atapp_external_ip }}:{{ $service_port }}"
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

{{- define "atapp.etcd.instance.settings.yaml" -}}
{{- $etcd := .etcd -}}
{{- $root := .root -}}
{{- $etcd_module := $root.Values.etcd | default (dict) -}}
{{- if $etcd }}
{{- /* Resolve cluster definition from etcd_clusters */ -}}
{{- $cluster_name := $etcd.cluster_name | default $etcd_module.etcd_default_cluster -}}
{{- $cluster := index ($etcd_module.etcd_clusters | default (dict)) ($cluster_name | default "") | default (dict) -}}
{{- $merge := mergeOverwrite (dict) $cluster $etcd -}}
enable: {{ $merge.enabled | default $etcd_module.enabled | default false }}
{{- /* etcd module enabled */}}
{{- if $merge.enabled | default $etcd_module.enabled | default false }}
{{- if $merge.client_urls | default $cluster.client_urls }}
hosts:
{{- range $_, $host := ($merge.client_urls | default $cluster.client_urls) }}
  - {{ $host }}
{{- end }} {{- /* end range client_urls */ -}}
{{- else }}
hosts:
{{- range $idx, $node := $root.Values.etcd_deploy_nodes }}
{{- $port := add ($merge.etcd_listen_client_base_port | default $cluster.etcd_listen_client_base_port) $node.Index }}
{{- $url := printf "http://%s:%d" $node.InnerIP $port }}
  - {{ $url }}
{{- end }} {{- /* end range etcd_deploy_nodes */ -}}
{{- end -}} {{- /* end if */}}
{{- $path := $merge.path | default $cluster.path -}}
{{- if $root.Values.partition }}
path: {{ $path }}/{{ include "libapp.environment" $root }}/{{ $root.Values.partition }}
{{- else }}
path: {{ $path }}
{{- end }}
{{- if empty ($merge.authorization | default $cluster.authorization) }}
# authorization:  # username:password
{{- else }}
authorization: "{{ $merge.authorization | default $cluster.authorization }}"
{{- end }}
{{- $ssl := $merge.ssl | default $cluster.ssl -}}
{{- if $ssl }}
ssl:
  enable_alpn: {{ $ssl.enable_alpn }}
  verify_peer: {{ $ssl.verify_peer }}
  ssl_min_version: {{ $ssl.ssl_min_version }}
  ssl_client_cert: "../../etcd/ssl/{{ $ssl.ssl_client_cert_file }}"
  ssl_client_key: "../../etcd/ssl/{{ $ssl.ssl_client_key_file }}"
{{- if empty $ssl.ssl_client_key_passwd }}
  # ssl_client_key_passwd:
{{- else }}
  ssl_client_key_passwd: {{ $ssl.ssl_client_key_passwd }}
{{- end }}
  ssl_ca_cert: ../../etcd/ssl/{{ $ssl.ssl_ca_cert_file }}
{{- if empty $ssl.ssl_cipher_list }}
  # ssl_cipher_list:
{{- else }}
  ssl_cipher_list: "{{ $ssl.ssl_cipher_list }}"
{{- end }}
{{- if empty $ssl.ssl_cipher_list_tls13 }}
  # ssl_cipher_list_tls13:
{{- else }}
  ssl_cipher_list_tls13: "{{ $ssl.ssl_cipher_list_tls13 }}"
{{- end }}
{{- end }} {{- /* end if $ssl */}}
{{- $cluster_cfg := $merge.cluster | default $cluster.cluster -}}
{{- if $cluster_cfg }}
cluster:
  auto_update: {{ $cluster_cfg.auto_update }}       # set false when etcd service is behind a safe cluster(Kubernetes etc.)
  update_interval: {{ $cluster_cfg.update_interval }}       # update etcd cluster members interval
  retry_interval: {{ $cluster_cfg.retry_interval }}       # update etcd cluster retry interval
{{- end }} {{- /* end if $cluster_cfg */}}
{{- $keepalive := mergeOverwrite (dict) ($cluster.keepalive | default (dict)) ($merge.keepalive | default (dict)) -}}
{{- if $keepalive }}
keepalive:
  enabled: {{ dig "enabled" true $keepalive }}
  timeout: {{ $keepalive.timeout }}            # expired timeout
  ttl: {{ $keepalive.ttl }}                # renew ttl interval
  retry_interval: {{ $keepalive.retry_interval }} # keepalive retry interval
{{- end }} {{- /* end if $keepalive */}}
{{- $request := $merge.request | default $cluster.request -}}
{{- if $request }}
request:
  timeout: {{ $request.timeout }}             # timeout for etcd request
  initialization_timeout: {{ $request.initialization_timeout }} # timeout for etcd request when initializing
  connect_timeout: {{ $request.connect_timeout }} # timeout for etcd request connect
  dns_cache_timeout: {{ $request.dns_cache_timeout }} # timeout for dns cache of etcd request
  dns_servers: "{{ $request.dns_servers }}" # dns servers: 8.8.8.8:53,1.1.1.1
{{- end }} {{- /* end if $request */}}
{{- $init := $merge.init | default $cluster.init -}}
{{- if $init }}
init:
  timeout: {{ $init.timeout }}                  # initialize timeout
  tick_interval: 256ms
{{- end }} {{- /* end if $init */}}
{{- $watcher := mergeOverwrite (dict) ($cluster.watcher | default (dict)) ($merge.watcher | default (dict)) -}}
{{- if $watcher }}
watcher:
  enabled: {{ dig "enabled" true $watcher }}
  retry_interval: {{ $watcher.retry_interval }}       # retry interval watch when previous request failed
  request_timeout: {{ $watcher.request_timeout }}       # request timeout for watching
  get_request_timeout: {{ $watcher.get_request_timeout }}            # range request timeout for watcher
  startup_random_delay_min: {{ $watcher.startup_random_delay_min }}  # delay start watching - min
  startup_random_delay_max: {{ $watcher.startup_random_delay_max }}  # delay start watching - max
  by_id: {{ $watcher.by_id }}       # watch service discovery by id
  by_name: {{ $watcher.by_name }}       # watch service discovery by name
  # by_type_id: []
  # by_type_name: []
  # by_tag: []
{{- end }} {{- /* end if $watcher */}}
{{- end }} {{- /* end if enabled */}}
{{- else }}
enable: false
{{- end }}
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
    {{- include "atapp.etcd.instance.settings.yaml" (dict "root" . "etcd" .Values.etcd) | trim | nindent 4 }}
    {{- $etcd_log := dig "log" (dict) .Values.etcd -}}
    {{- if and .Values.etcd (dig "enabled" false .Values.etcd) $etcd_log.enable }}
    log:
      startup_level: {{ $etcd_log.startup_level }}
      level: {{ $etcd_log.level }}
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
    {{- end }}
  external_discovery:
  {{- if .Values.external_discovery }}
  {{- range .Values.external_discovery }}
    - {{ include "atapp.etcd.instance.settings.yaml" (dict "root" $ "etcd" .) | trim | nindent 6 | trim }}
  {{- end }}
  {{- end }}
  {{- if eq .Values.type_name "atproxy" }}
  {{- $main_cluster := .Values.etcd.cluster_name | default .Values.etcd.etcd_default_cluster }}
  {{- range $cluster_name, $_ := .Values.etcd.etcd_clusters }}
  {{- if ne $cluster_name $main_cluster }}
  {{- $skip := dict "value" false }}
  {{- range $.Values.external_discovery }}
  {{- $ext_name := .cluster_name | default $.Values.etcd.etcd_default_cluster }}
  {{- if eq $cluster_name $ext_name }}
  {{- $_ := set $skip "value" true }}
  {{- end }}
  {{- end }}
  {{- if not (get $skip "value") }}
  {{- $cfg := dict "cluster_name" $cluster_name "keepalive" (dict "enabled" false) }}
    - {{ include "atapp.etcd.instance.settings.yaml" (dict "root" $ "etcd" $cfg) | trim | nindent 6 | trim }}
  {{- end }}
  {{- end }}
  {{- end }}
  {{- else }}
  {{- $etcd_cfg := dict "cluster_name" .Values.etcd.etcd_default_proxy_cluster "keepalive" (dict "enabled" false) }}
    - {{ include "atapp.etcd.instance.settings.yaml" (dict "root" . "etcd" $etcd_cfg) | trim | nindent 6 | trim }}
  {{- end }}
{{- end }}
