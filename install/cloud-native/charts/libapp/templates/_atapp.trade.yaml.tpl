{{- define "atapp.trade.yaml" -}}
{{- $values := deepCopy .Values -}}
{{- $trade := $values.trade | default (dict) -}}
{{- $role := trimPrefix "tradesvr-" .Chart.Name -}}
{{- $role_hpa := get ($trade.hpa | default (dict)) $role | default (dict) -}}
{{- $_ := set $values "hpa" (mergeOverwrite ($values.hpa | default (dict)) $role_hpa) -}}
{{- $context := merge (dict "Values" $values) (omit . "Values") -}}
{{ include "atapp.yaml" $context }}
{{ include "atapp.logic.yaml" $context }}
{{- $services := $trade.service | default (dict) -}}
{{- $settings := get $services $role | default (dict) -}}
{{- $_ := set $settings "shared_setting" (get $services "common" | default (dict)) }}
tradesvr_{{ $role }}:
  {{- toYaml $settings | nindent 2 }}
{{- end }}
