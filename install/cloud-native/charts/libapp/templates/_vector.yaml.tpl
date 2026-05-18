{{- define "vector.yaml" -}}
sources:
{{- range $src := .Values.vector.source }}
  {{ $.Values.type_name }}_{{ $src.name }}:
    type: file
    include:
      - {{ $.Values.vector.log_path }}/{{ $src.include_pattern }}
    data_dir: {{ $.Values.vector.log_path }}
    ignore_older_secs: {{ $src.ignore_older_secs | default 600 }}
    read_from: beginning
    line_delimiter: {{ $src.line_delimiter | toJson }}
    glob_minimum_cooldown_ms: 1000
{{- end }}

transforms:
{{- range $src := .Values.vector.source }}
  {{ $src.name }}_enrich:
    type: remap
    inputs:
      - {{ $.Values.type_name }}_{{ $src.name }}
    source: |
{{- if eq $src.remap.parse_type "normal" }}
{{ include "libapp.vector.server_log_normal" ($src.name) | indent 6 }}
{{- if $src.remap.index }}
{{ include "libapp.vector.server_log_index" $src.remap | indent 6 }}
{{- end }}
{{- else if eq $src.remap.parse_type "actor" }}
{{ include "libapp.vector.server_log_actor" . | indent 6 }}
{{- else if eq $src.remap.parse_type "oss" }}
{{ include "libapp.vector.server_log_oss" . | indent 6 }}
{{- else if eq $src.remap.parse_type "crash" }}
{{ include "libapp.vector.server_log_crash" . | indent 6 }}
{{- end }}
{{- end }}

sinks:
{{- if .Values.vector.sliks.console.enable }}
  console:
    type: console
    inputs:
    {{- range $src := .Values.vector.source }}
      - {{ $src.name }}_enrich
    {{- end }}
    encoding:
      codec: json
{{- end }}
{{- if .Values.vector.sliks.test_file.enable }}
  file:
    type: file
    inputs:
    {{- range $src := .Values.vector.source }}
      - {{ $src.name }}_enrich
    {{- end }}
    path: {{ .Values.vector.log_path }}/vector_output.log
    encoding:
      codec: json
{{- end }}
{{- if and .Values.vector.sliks.opensearch .Values.vector.sliks.opensearch.enable }}
{{- $seenSinks := dict }}
{{- range $src := .Values.vector.source }}
{{- if and $src.sinks $src.sinks.opensearch }}
{{- $sinkName := $src.sinks.sink_name }}
{{- if not (hasKey $seenSinks $sinkName) }}
{{- $_ := set $seenSinks $sinkName true }}
  opensearch_log_{{ $sinkName }}:
    type: elasticsearch
    mode: data_stream
    inputs:
    {{- range $inner := $.Values.vector.source }}
    {{- if and $inner.sinks $inner.sinks.opensearch }}
    {{- if eq $inner.sinks.sink_name $sinkName }}
      - {{ $inner.name }}_enrich
    {{- end }}
    {{- end }}
    {{- end }}
    endpoints:
      - {{ $.Values.vector.sliks.opensearch.endpoint }}
    auth:
      strategy: basic
      user: {{ $.Values.vector.sliks.opensearch.username }}
      password: {{ $.Values.vector.sliks.opensearch.password }}
    tls:
      verify_certificate: false
      verify_hostname: false
    data_stream:
      type: {{ $.Values.vector.sliks.opensearch.data_stream_type }}
      dataset: {{ $src.sinks.opensearch.dataset }}
      namespace: {{ $src.sinks.opensearch.namespace }}
{{- end }}
{{- end }}
{{- end }}
{{- end -}}
{{- if and .Values.vector.sliks.kafka .Values.vector.sliks.kafka.enable }}
{{- $seenSinks := dict }}
{{- range $src := .Values.vector.source }}
{{- if and $src.sinks $src.sinks.kafka }}
{{- $sinkName := $src.sinks.sink_name }}
{{- if not (hasKey $seenSinks $sinkName) }}
{{- $_ := set $seenSinks $sinkName true }}
  kafka_log_{{ $sinkName }}:
    type: kafka
    inputs:
    {{- range $inner := $.Values.vector.source }}
    {{- if and $inner.sinks $inner.sinks.opensearch }}
    {{- if eq $inner.sinks.sink_name $sinkName }}
      - {{ $inner.name }}_enrich
    {{- end }}
    {{- end }}
    {{- end }}
    bootstrap_servers: {{ $.Values.vector.sliks.kafka.bootstrap_servers }}
    compression: gzip
    healthcheck: true
    topic: {{ $src.sinks.kafka.logstore }}
    encoding:
      codec: json
    sasl:
      enabled: true
      mechanism: PLAIN
      username: {{ $.Values.vector.sliks.kafka.project }}
      password: {{ $.Values.vector.sliks.kafka.password }}
    tls:
      enabled: true
{{- end }}
{{- end }}
{{- end }}
{{- end -}}
{{- end }}