{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.friendsvr_management }}
friendsvr_management:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
