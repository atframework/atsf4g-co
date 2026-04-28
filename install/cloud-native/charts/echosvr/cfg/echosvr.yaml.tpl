{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.echosvr }}
echosvr:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
