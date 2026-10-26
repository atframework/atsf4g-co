{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

compliance_svr:
  {{- toYaml .Values.compliance_svr | trim | nindent 2 }}
