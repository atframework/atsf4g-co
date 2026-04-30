{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.ranksvr-settlement }}
ranksvr-settlement:
  {{- toYaml .Values.ranksvr_settlement_svr | trim | nindent 2 }}
{{- end }}
