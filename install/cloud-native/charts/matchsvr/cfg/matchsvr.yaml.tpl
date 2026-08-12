{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.matchsvr }}
matchsvr:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
