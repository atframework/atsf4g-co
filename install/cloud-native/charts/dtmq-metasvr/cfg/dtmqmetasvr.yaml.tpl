{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.dtmq_metasvr }}
dtmq_metasvr:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
