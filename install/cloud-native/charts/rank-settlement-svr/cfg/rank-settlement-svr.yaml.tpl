{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.ranksvr_settlement }}
ranksvr_settlement:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
