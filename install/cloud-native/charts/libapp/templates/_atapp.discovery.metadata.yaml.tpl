{{- define "atapp.discovery.metadata.yaml" -}}
{{- if not (empty .namespace_name) }}
namespace_name: "{{ .namespace_name }}"
{{- end }} {{- /* end if namespace_name */}}
{{- if not (empty .api_version) }}
api_version: "{{ .api_version }}"
{{- end }} {{- /* end if api_version */}}
{{- if not (empty .kind) }}
kind: "{{ .kind }}"
{{- end }} {{- /* end if kind */}}
{{- if not (empty .group) }}
group: "{{ .group }}"
{{- end }} {{- /* end if group */}}
{{- if not (empty .service_subset) }}
service_subset: "{{ .service_subset }}"
{{- end }} {{- /* end if service_subset */}}
{{- if not (empty .labels) }}
labels:
{{- range $label_key, $label_value := .labels }}
  "{{ $label_key }}" : "{{ $label_value }}"
{{- end }} {{- /* end range labels */}}
{{- end }} {{- /* end if labels */}}
{{- if not (empty .annotations) }}
annotations:
{{- range $annotation_key, $annotation_value := .annotations }}
  "{{ $annotation_key }}" : "{{ $annotation_value }}"
{{- end }} {{- /* end range annotations */}}
{{- end }} {{- /* end if annotations */}}
{{- end }}
{{- define "atapp.default.metadata.yaml" -}}
labels:
  # deployment.environment is deprecated by otel, but keep it for compatibility
  "deployment.environment.name": "{{ .Values.atapp.deployment.deployment_environment | default "" }}" # formal-qq/formal-wx/daily/qatest*/t-*
{{- end }}