{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.cachesvr }}
cachesvr:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
