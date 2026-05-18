{{ include "atapp.yaml" . }}

atgateway:
  # listen address for client to connect, how to use it depends listen.type
  listen:
    address:
      - ipv4://0.0.0.0:{{ add (.Values.atgateway.listen.begin_port | default 8000 | int64) (.Values.instance_id | default 1 | int64) }}
    type: inner                     # protocol type
    max_client: 65536               # max client number, more client will be closed
    backlog: 128

  client:
    reconnect_timeout: 180          # reconnect timeout
    first_idle_timeout: 10          # first idle timeout
    recv_buffer_size: 2MB           # recv buffer limit
    send_buffer_size: 4MB           # send buffer limit

    {{- with (dig "default_router" false .Values.atgateway ) }}
    default_router:
      {{- toYaml . | trim | nindent 6 }}
    {{- end }}

    {{- with (dig "crypto" false .Values.atgateway ) }}
    crypto:
      {{- toYaml . | trim | nindent 6 }}
    {{- end }}

  echo_server: {{ .Values.atgateway.echo_server }}
