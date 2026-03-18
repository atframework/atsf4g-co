{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

lobbysvr:
  webserver:
    port: {{ .Values.webserver.port }}
  websocket:
    {{- $atapp := (default (dict) .Values.atapp) -}}
    {{- $deploy := (default (dict) $atapp.deployment) -}}
    {{- $env := (default "" $deploy.deployment_environment) -}}
    {{- $ws := (default (dict) .Values.websocket) -}}
    {{- $wspath := (default "" $ws.path) -}}
    {{- if $wspath }}
    path: {{ printf "/%s/%s/ws/v1" $env $wspath | replace "///" "/" | replace "//" "/" | quote }}
    {{- else }}
    path: "/ws/v1"
    {{- end }}
