{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

{{- with .Values.ranksvr_ranking }}
ranksvr_ranking:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
