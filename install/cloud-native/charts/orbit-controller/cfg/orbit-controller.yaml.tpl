{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}                         # relogin to the same gamesvr in 4 hours relogin

{{- with .Values.orbit_controller }}
orbit_controller:
  {{- toYaml . | trim | nindent 2 }}
{{- end }}
