{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.friendsvr_recommend }}
friendsvr_recommend:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
