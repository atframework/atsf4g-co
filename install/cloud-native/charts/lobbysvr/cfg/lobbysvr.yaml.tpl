{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.lobbysvr }}
lobbysvr:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
