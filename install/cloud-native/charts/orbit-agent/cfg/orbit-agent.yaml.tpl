{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}                         # relogin to the same gamesvr in 4 hours relogin
{{- $service_port := include "libapp.atbus.calculateServicePort" . -}}

orbit_agent:
  region: {{ .Values.orbit_agent.region }}
  tag: {{ .Values.orbit_agent.tag }}
  configured_client_command_line: {{ .Values.orbit_agent.configured_client_command_line }}
  cpu_capacity: {{ .Values.orbit_agent.cpu_capacity }}
  memory_capacity_mb: {{ .Values.orbit_agent.memory_capacity_mb }}
  server_identity_timeout_sec: {{ .Values.orbit_agent.server_identity_timeout_sec }}
  server_identity_check_interval_sec: {{ .Values.orbit_agent.server_identity_check_interval_sec }}
  max_batch_startup_count: {{ .Values.orbit_agent.ax_batch_startup_count }}
  client_ip: {{ .Values.orbit_agent.host_name }}
  remote_agent_addr: dns://{{ .Values.orbit_agent.host_name }}:{{ $service_port }}
  enable_seed_mode: {{ .Values.orbit_agent.enable_seed_mode }}
  seed_startup_timeout_sec: {{ .Values.orbit_agent.seed_startup_timeout_sec }}
  seed_heartbeat_timeout_sec: {{ .Values.orbit_agent.seed_heartbeat_timeout_sec }}
  seed_client_command_line: {{ .Values.orbit_agent.seed_client_command_line }}