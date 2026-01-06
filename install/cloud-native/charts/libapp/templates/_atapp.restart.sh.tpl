{{- define "atapp.restart.sh" -}}
{{ include "atapp.stop.sh" . }}
{{ include "atapp.start.sh" . }}
{{- end }}

{{- define "atapp.restart.bat" -}}
{{ include "atapp.stop.bat" . }}
{{ include "atapp.start.bat" . }}
{{- end }}