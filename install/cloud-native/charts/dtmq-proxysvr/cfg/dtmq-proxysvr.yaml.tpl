{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.dtmq_proxysvr }}
dtmq_proxysvr:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
