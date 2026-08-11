{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.teamsvr_room }}
teamsvr_room:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
