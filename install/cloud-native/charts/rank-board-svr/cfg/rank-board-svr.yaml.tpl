{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.ranksvr-ranking }}
ranksvr-ranking:
  {{- toYaml .Values.ranksvr_ranking_svr | trim | nindent 2 }}
{{- end }}
