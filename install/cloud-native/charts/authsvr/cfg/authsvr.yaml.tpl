{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}                         # relogin to the same gamesvr in 4 hours relogin

{{- with .Values.authsvr }}
authsvr:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
