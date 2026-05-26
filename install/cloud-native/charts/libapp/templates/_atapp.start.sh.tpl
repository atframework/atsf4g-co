{{- define "atapp.start.sh" -}}
{{ include "atapp.start.prepare.sh" . }}
{{ include "atapp.start.run.sh" . }}
{{- end }}