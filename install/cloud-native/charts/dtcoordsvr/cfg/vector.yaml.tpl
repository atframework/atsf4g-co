sources:
  dtcoordsvr_crash:
    type: file
    include:
      - {{ .Values.vector.log_path }}/*.crash.log
    data_dir: {{ .Values.vector.log_path }}
    ignore_older_secs: 3600
    read_from: beginning
    line_delimiter: "\n"
    glob_minimum_cooldown_ms: 1000

  dtcoordsvr_logs_normal:
    type: file
    include:
      - {{ .Values.vector.log_path }}/*.normal.all.*.log
    data_dir: {{ .Values.vector.log_path }}
    ignore_older_secs: 600
    read_from: beginning
    line_delimiter: "\u001e\n"
    glob_minimum_cooldown_ms: 1000

  dtcoordsvr_logs_db_inner:
    type: file
    include:
      - {{ .Values.vector.log_path }}/*.db_inner.all.*.log
    data_dir: {{ .Values.vector.log_path }}
    ignore_older_secs: 600
    read_from: beginning
    line_delimiter: "\u001e\n"
    glob_minimum_cooldown_ms: 1000

  dtcoordsvr_logs_redis:
    type: file
    include:
      - {{ .Values.vector.log_path }}/*.redis.all.*.log
    data_dir: {{ .Values.vector.log_path }}
    ignore_older_secs: 600
    read_from: beginning
    line_delimiter: "\u001e\n"
    glob_minimum_cooldown_ms: 1000

  dtcoordsvr_logs_actor:
    type: file
    include:
      - {{ .Values.vector.log_path }}/*/*-*.*.log
    data_dir: {{ .Values.vector.log_path }}
    ignore_older_secs: 600
    read_from: beginning
    line_delimiter: "\u001e\n"
    glob_minimum_cooldown_ms: 1000

  dtcoordsvr_logs_mon:
    type: file
    include:
      - {{ .Values.vector.log_path }}/*.mon.*.log
    data_dir: {{ .Values.vector.log_path }}
    ignore_older_secs: 3600
    read_from: beginning
    line_delimiter: "\u001e\n"
    glob_minimum_cooldown_ms: 1000

  dtcoordsvr_logs_oss:
    type: file
    include:
      - {{ .Values.vector.log_path }}/*.oss.*.log
    data_dir: {{ .Values.vector.log_path }}
    ignore_older_secs: 3600
    read_from: beginning
    line_delimiter: "\u001e\n"
    glob_minimum_cooldown_ms: 1000

transforms:
  actor_enrich:
    type: remap
    inputs:
      - dtcoordsvr_logs_actor
    source: |
      .file_path = string!(.file)
      . |= parse_regex!(.file_path, r'(?:^|[\\/])dtcoordsvr[\\/]log[\\/][^\\/]+[\\/](?P<zone_id>\d+)-(?P<user_id>\d+)')
      del(.timestamp)
      del(.log)
      del(.file)
      del(.file_path)
      del(.host)
      del(.source_type)
      . |= parse_regex!(.message, r'(?P<log_ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})')
      parsed_ts = parse_timestamp!(.log_ts, "%Y-%m-%d %H:%M:%S%.f", timezone: "Asia/Shanghai")
      .log_ts = format_timestamp!(parsed_ts, format: "%FT%T.%3fZ", timezone: "UTC")

  normal_enrich:
    type: remap
    inputs:
      - dtcoordsvr_logs_normal
    source: |
{{ include "libapp.vector.server_log_parse" "normal" | indent 6 }}
{{ include "libapp.vector.server_log_index" . | indent 6 }}

  db_inner_enrich:
    type: remap
    inputs:
      - dtcoordsvr_logs_db_inner
    source: |
{{ include "libapp.vector.server_log_parse" "db_inner" | indent 6 }}

  redis_enrich:
    type: remap
    inputs:
      - dtcoordsvr_logs_redis
    source: |
{{ include "libapp.vector.server_log_parse" "redis" | indent 6 }}

  crash_enrich:
    type: remap
    inputs:
      - dtcoordsvr_crash
    source: |
      .file_path = string!(.file)
      .file_name = basename!(.file_path)
      . |= parse_regex!(.file_name, r'^(?P<svrname>[A-Za-z0-9_-]+)_(?P<inst_id>\d+.\d+.\d+.\d+)')
      del(.file)
      del(.file_path)
      del(.file_name)
      del(.host)
      del(.source_type)
      del(.timestamp)
      .log_ts = now()

  mon_enrich:
    type: remap
    inputs:
      - dtcoordsvr_logs_mon
    source: |
      del(.host)
      del(.source_type)
      del(.timestamp)
      del(.file)
      parse_data = parse_regex!(.message, r'^(?P<log_ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})(?P<message>\{.*\})$')
      parsed_ts = parse_timestamp!(parse_data.log_ts, "%Y-%m-%d %H:%M:%S", timezone: "Asia/Shanghai")
      parse_data.log_ts = format_timestamp!(parsed_ts, format: "%FT%T.000Z", timezone: "UTC")
      . = parse_json!(parse_data.message)
      .log_ts = parse_data.log_ts

  oss_enrich:
    type: remap
    inputs:
      - dtcoordsvr_logs_oss
    source: |
      del(.host)
      del(.source_type)
      del(.timestamp)
      del(.file)
      parse_data = parse_regex!(.message, r'^(?P<log_ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})(?P<message>\{.*\})$')
      parsed_ts = parse_timestamp!(parse_data.log_ts, "%Y-%m-%d %H:%M:%S", timezone: "Asia/Shanghai")
      parse_data.log_ts = format_timestamp!(parsed_ts, format: "%FT%T.000Z", timezone: "UTC")
      . = parse_json!(parse_data.message)
      .log_ts = parse_data.log_ts

sinks:
  {{- if .Values.vector.sliks.console.enable }}
  console:
    type: console
    inputs:
      - actor_enrich
      - normal_enrich
      - db_inner_enrich
      - redis_enrich
      - dtcoordsvr_crash
      - mon_enrich
      - oss_enrich
    encoding:
      codec: json
  {{- end}}
  {{- if .Values.vector.sliks.test_file.enable }}
  file:
    type: file
    inputs:
      - actor_enrich
      - normal_enrich
      - db_inner_enrich
      - redis_enrich
      - dtcoordsvr_crash
      - mon_enrich
      - oss_enrich
    path: {{ .Values.vector.log_path }}/vector_output.log
    encoding:
      codec: json
  {{- end}}
  {{- if .Values.vector.sliks.opensearch.enable }}
  opensearch_log_crash:
    type: elasticsearch
    mode: data_stream
    inputs:
      - crash_enrich
    endpoints:
      - {{ .Values.vector.sliks.opensearch.endpoint }}
    auth:
      strategy: basic
      user: {{ .Values.vector.sliks.opensearch.username }}
      password: {{ .Values.vector.sliks.opensearch.password }}
    tls:
      verify_certificate: false
      verify_hostname: false
    data_stream:
      type: project-x
      dataset: log
      namespace: crash

  opensearch_log_running:
    type: elasticsearch
    mode: data_stream
    inputs:
      - normal_enrich
      - db_inner_enrich
      - redis_enrich
    endpoints:
      - {{ .Values.vector.sliks.opensearch.endpoint }}
    auth:
      strategy: basic
      user: {{ .Values.vector.sliks.opensearch.username }}
      password: {{ .Values.vector.sliks.opensearch.password }}
    tls:
      verify_certificate: false
      verify_hostname: false
    data_stream:
      type: project-x
      dataset: log
      namespace: running

  opensearch_log_actor:
    type: elasticsearch
    mode: data_stream
    inputs:
      - actor_enrich
    endpoints:
      - {{ .Values.vector.sliks.opensearch.endpoint }}
    auth:
      strategy: basic
      user: {{ .Values.vector.sliks.opensearch.username }}
      password: {{ .Values.vector.sliks.opensearch.password }}
    tls:
      verify_certificate: false
      verify_hostname: false
    data_stream:
      type: project-x
      dataset: log
      namespace: actor

  opensearch_log_oss:
    type: elasticsearch
    mode: data_stream
    inputs:
      - oss_enrich
    endpoints:
      - {{ .Values.vector.sliks.opensearch.endpoint }}
    auth:
      strategy: basic
      user: {{ .Values.vector.sliks.opensearch.username }}
      password: {{ .Values.vector.sliks.opensearch.password }}
    tls:
      verify_certificate: false
      verify_hostname: false
    data_stream:
      type: project-x
      dataset: oss
      namespace: type-{{ "{{ log_type }}" }}

  opensearch_log_mon:
    type: elasticsearch
    mode: data_stream
    inputs:
      - mon_enrich
    endpoints:
      - {{ .Values.vector.sliks.opensearch.endpoint }}
    auth:
      strategy: basic
      user: {{ .Values.vector.sliks.opensearch.username }}
      password: {{ .Values.vector.sliks.opensearch.password }}
    tls:
      verify_certificate: false
      verify_hostname: false
    data_stream:
      type: project-x
      dataset: mon
      namespace: all
  {{- end -}}