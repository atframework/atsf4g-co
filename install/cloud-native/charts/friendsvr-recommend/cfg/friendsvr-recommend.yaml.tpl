{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.teamsvr_match }}
teamsvr_match:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
