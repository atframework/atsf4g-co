master-addr: {{ .Values.master_addr }}
redis-addr: {{ .Values.report_redis_addr }}
redis-pwd: {{ .Values.report_redis_password }}
cluster-mode: false

url: localhost:{{ add (.Values.atgateway.listen.begin_port | default 8000 | int64) (.Values.instance_id | default 1 | int64) }}
connect-type: atgateway
access-token: {{ .Values.atgateway_access_tokens }}
key-exchange: x25519
crypto: aes-256-gcm
compression: zstd
resource: ../../resource/excel

set:
  openIDPrefix: 1250000

dbtool-redis-addr:
{{- range $_, $addr := .Values.redis.addrs }}
  - {{ $addr }}
{{- end }}
dbtool-redis-password: {{ .Values.redis.password }}
dbtool-redis-cluster: {{ .Values.redis.cluster_mode }}
dbtool-random-prefix: {{ .Values.redis.record_prefix }}
dbtool-pb-file: {{ .Values.db_pb_path }}
dbtool-redis-version: {{ .Values.redis_version }}