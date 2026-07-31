{{- define "atapp.logic.hpa_policy.yaml" -}}
metrics_name: "{{ .metrics_name }}"
  {{- if not (empty .policy.metrics_unit) }}
metrics_unit: {{ .policy.metrics_unit }}
  {{- end }}
  {{- if not (empty .policy.metrics_description) }}
metrics_description: "{{ .policy.metrics_description }}"
  {{- end }}
  {{- if not (empty .policy.metrics_type) }}
metrics_type: {{ .policy.metrics_type }}
  {{- end }}
  {{- if not (empty .policy.aggregation) }}
aggregation: {{ .policy.aggregation }}
  {{- end }}
  {{- if not (empty .policy.aggregation_parameter) }}
aggregation_parameter:
    {{- if not (empty .policy.aggregation_parameter.by) }}
  by:
    labels:
      {{- range $_, $label_value := .policy.aggregation_parameter.by }}
      - {{ $label_value }}
      {{- end }}
    {{- else if not (empty .policy.aggregation_parameter.without) }}
  without:
    labels:
      {{- range $_, $label_value := .policy.aggregation_parameter.without }}
      - {{ $label_value }}
      {{- end }}
    {{- else if not (empty .policy.aggregation_parameter.k) }}
  k: {{ .policy.aggregation_parameter.k }}
    {{- else if not (empty .policy.aggregation_parameter.count_values) }}
  count_values:
    as: {{ .policy.aggregation_parameter.count_values.as | default "" }}
    target: {{ .policy.aggregation_parameter.count_values.target | default "" }}
    {{- end }}
  {{- end }}
  {{- if not (empty .policy.simple_function) }}
simple_function:
  {{- range $_, $function_value := .policy.simple_function }}
  - {{ toYaml $function_value | indent 4 | trim }}
  {{- end }}
  {{- end }}
  {{- if not (empty .policy.labels) }}
labels:
  {{- range $label_key, $label_value := .policy.labels }}
  "{{ $label_key }}": "{{ $label_value }}"
  {{- end }}
  {{- end }}
  {{- if not (empty .policy.selectors) }}
selectors:
  {{- range $label_key, $label_value := .policy.selectors }}
  "{{ $label_key }}": "{{ $label_value }}"
  {{- end }}
  {{- end }}
  {{- if not (empty .policy.without_auto_selectors) }}
without_auto_selectors:
  {{- range $_, $label_key := .policy.without_auto_selectors }}
  - {{ $label_key }}
  {{- end }}
  {{- end }}
  {{- if not (empty .policy.query) }}
query: "{{ .policy.query }}"
  {{- end }}
  {{- if not (empty .policy.scaling_up_value) }}
scaling_up_value: {{ .policy.scaling_up_value }}
  {{- end }}
  {{- if not (empty .policy.scaling_down_value) }}
scaling_down_value: {{ .policy.scaling_down_value }}
  {{- end }}
{{- end }}
{{- define "atapp.logic.hpa.yaml" -}}
  {{- if not (empty .hpa) }}
hpa:
  controller:
    {{- if not (empty .hpa.controller) }}
    enable: {{ .hpa.controller.enable | default "false" }}
    {{- else }}
    enable: false
    {{- end }}
    {{- if not (empty .hpa.controller) }}
    configure_key: "{{ .hpa.controller.configure_key | default "/atapp/hpa" }}"
    {{- else }}
    configure_key: "/atapp/hpa"
    {{- end }}
    {{- if not (empty .libapp_type_id) }}
    type_id: {{ .libapp_type_id }}
    {{- end }}
    type_name: {{ .libapp_name }}
    {{- if not (empty .hpa.controller) }}
    target:
      {{- if not (empty .hpa.controller.target) }}
        {{- if not (empty .hpa.controller.target.kind) }}
      kind: "{{ .hpa.controller.target.kind }}"
        {{- end }}
        {{- if not (empty .hpa.controller.target.api_version) }}
      api_version: "{{ .hpa.controller.target.api_version }}"
        {{- end }}
        {{- if not (empty .hpa.controller.target.name) }}
      name: "{{ .hpa.controller.target.name }}"
        {{- else }}
      name: "{{ .libapp_hpa_target_name }}"
        {{- end }}
      {{- else }}
      name: "{{ .libapp_hpa_target_name }}"
      {{- end }}
      {{- if not (empty .hpa.controller.discovery_labels) }}
    discovery_labels:
        {{- range $label_key, $label_value := .hpa.controller.discovery_labels }}
      "{{ $label_key }}": "{{ $label_value }}"
        {{- end }}
      {{- end }}
      {{- if .autoscaling.enabled }}
    min_replicas: {{ .autoscaling.minReplicas | default (.hpa.controller.min_replicas | default 2) }}
    max_replicas: {{ .autoscaling.maxReplicas | default (.hpa.controller.max_replicas | default 3000) }}
      {{- else }}
    min_replicas: {{ .hpa.controller.min_replicas | default 2 }}
    max_replicas: {{ .hpa.controller.max_replicas | default 3000 }}
      {{- end }}
      {{- if not (empty .hpa.controller.replicate_start_delay) }}
    replicate_start_delay: {{ .hpa.controller.replicate_start_delay }}
      {{- end }}
      {{- if not (empty .hpa.controller.replicate_period) }}
    replicate_period: {{ .hpa.controller.replicate_period }}
      {{- end }}
      {{- if not (empty .hpa.controller.scaling_delay) }}
    scaling_delay: {{ .hpa.controller.scaling_delay }}
      {{- end }}
    {{- end }}
    {{- if not (empty .hpa.metrics) }}
  metrics:
    enable: {{ .hpa.metrics.enable | default "false" }}
    pull_interval: {{ .hpa.metrics.pull_interval | default "180s" }}
    pull_retry_interval: {{ .hpa.metrics.pull_retry_interval | default "30s" }}
      {{- if not (empty .hpa.metrics.pull_request) }}
    pull_request:
      debug_mode: {{ .hpa.metrics.pull_request.debug_mode | default "false" }}
        {{- if empty .hpa.metrics.pull_request.timeout }}
      # timeout: "10s"
        {{- else }}
      timeout: {{ .hpa.metrics.pull_request.timeout }}
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.keepalive_timeout }}
      # keepalive_timeout: "600s"
        {{- else }}
      keepalive_timeout: {{ .hpa.metrics.pull_request.keepalive_timeout }}
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.connect_timeout }}
      # connect_timeout: "600s"
        {{- else }}
      connect_timeout: {{ .hpa.metrics.pull_request.connect_timeout }}
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.dns_cache_timeout }}
      # dns_cache_timeout: "600s"
        {{- else }}
      dns_cache_timeout: {{ .hpa.metrics.pull_request.dns_cache_timeout }}
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.dns_servers }}
      # dns_servers: "8.8.8.8,1.1.1.1" # host[:port][,host[:port]]...
        {{- else }}
      dns_servers: "{{ .hpa.metrics.pull_request.dns_servers }}" # host[:port][,host[:port]]...
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.user_agent }}
      # user_agent: ""
        {{- else }}
      user_agent: "{{ .hpa.metrics.pull_request.user_agent }}"
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.proxy }}
      # proxy: ""
        {{- else }}
      proxy: "{{ .hpa.metrics.pull_request.proxy }}"
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.no_proxy }}
      # no_proxy: ""
        {{- else }}
      no_proxy: "{{ .hpa.metrics.pull_request.no_proxy }}"
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.proxy_user_name }}
      # proxy_user_name: ""
        {{- else }}
      proxy_user_name: "{{ .hpa.metrics.pull_request.proxy_user_name }}"
        {{- end }}
        {{- if empty .hpa.metrics.pull_request.proxy_password }}
      # proxy_password: ""
        {{- else }}
      proxy_password: "{{ .hpa.metrics.pull_request.proxy_password }}"
        {{- end }}
      {{- end }}
      {{- if not (empty .hpa.metrics.pull_request) }}
    pull_ssl:
        {{- printf "\n%v" (toYaml .hpa.metrics.pull_ssl | indent 6) }}
      {{- end }}
    pull_metrics_name_mode: {{ .hpa.metrics.pull_metrics_name_mode | default "name_and_unit" }}
    # job and deployment_environment will be auto added into labels and selectors and instance will be auto added into labels
    #   and follow the specification of
    # https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/compatibility/prometheus_and_openmetrics.md
    # hpa_target_name,hpa_target_kind and hpa_target_api_version will be auto added to labels and selectors with values
    #   of hpa.controller.target.name, hpa.controller.target.kind and hpa.controller.target.api_version
      {{- if not (empty .hpa.metrics.labels) }}
    labels:
        {{- range $label_key, $label_value := .hpa.metrics.labels }}
      "{{ $label_key }}": "{{ $label_value }}"
        {{- end }}
      {{- else }}
    # labels:
      {{- end }}
      {{- if not (empty .hpa.metrics.selectors) }}
    selectors:
        {{- range $label_key, $label_value := .hpa.metrics.selectors }}
      "{{ $label_key }}": "{{ $label_value }}"
        {{- end }}
      {{- else }}
    # selectors:
      {{- end }}
      {{- if not (empty .hpa.metrics.without_auto_selectors) }}
    without_auto_selectors:
        {{- range $_, $label_key := .hpa.metrics.without_auto_selectors }}
      - {{ $label_key }}
        {{- end }}
      {{- end }}
    {{- end }}
    metrics_name_expect_replicas: "{{ .hpa.metrics.metrics_name_expect_replicas | default "atframework_hpa_expect_replicas" }}"
    metrics_name_stateful_index: "{{ .hpa.metrics.metrics_name_stateful_index | default "atframework_hpa_stateful_index" }}"
  rule:
    {{- if not (empty .hpa.rule) }}
    set_scaling_label: {{ dig "set_scaling_label" true .hpa.rule }}
    scaling_up_configure:
      stabilization_window: {{ dig "scaleUp" "stabilizationWindowSeconds" (dig "rule" "scaling_up_configure" "stabilization_window" "180s" .hpa) .autoscaling }}
      {{- with (dig "rule" "scaling_up_configure" "max_scaling_count" false .hpa) }}
      max_scaling_count: {{ . }}
      {{- end }}
    scaling_down_configure:
      stabilization_window: {{ dig "scaleUp" "stabilizationWindowSeconds" (dig "rule" "scaling_down_configure" "stabilization_window" "180s" .hpa) .autoscaling }}
      {{- with (dig "rule" "scaling_down_configure" "max_scaling_count" false .hpa) }}
      max_scaling_count: {{ . }}
      {{- end }}
    cpu_permillage:
      {{- if not (empty .hpa.rule.cpu_permillage) }}
        {{- include "atapp.logic.hpa_policy.yaml" (dict "autoscaling" .autoscaling "metrics_name" (.hpa.rule.cpu_permillage.metrics_name | default "atframework_hpa_rule_cpu") "policy" .hpa.rule.cpu_permillage) | nindent 6 }}
        {{- if empty .hpa.rule.cpu_permillage.metrics_unit }}
      metrics_unit: "permillage"
        {{- end }}
        {{- if empty .hpa.rule.cpu_permillage.metrics_description }}
      metrics_description: "User CPU time and system CPU time used permillage"
        {{- end }}
      {{- else }}
      metrics_name: "atframework_hpa_rule_cpu"
      metrics_unit: "permillage"
      metrics_description: "User CPU time and system CPU time used permillage"
      {{- end }}
    main_thread_cpu_permillage:
      {{- if not (empty .hpa.rule.main_thread_cpu_permillage) }}
        {{- include "atapp.logic.hpa_policy.yaml" (dict "autoscaling" .autoscaling "metrics_name" (.hpa.rule.main_thread_cpu_permillage.metrics_name | default "atframework_hpa_rule_main_thread_cpu") "policy" .hpa.rule.main_thread_cpu_permillage) | nindent 6 }}
        {{- if empty .hpa.rule.main_thread_cpu_permillage.metrics_unit }}
      metrics_unit: "permillage"
        {{- end }}
        {{- if empty .hpa.rule.main_thread_cpu_permillage.metrics_description }}
      metrics_description: "User CPU time and system CPU time used permillage"
        {{- end }}
      {{- else }}
      metrics_name: "atframework_hpa_rule_main_thread_cpu"
      metrics_unit: "permillage"
      metrics_description: "User CPU time and system CPU time used permillage"
      {{- end }}
    memory:
      {{- if not (empty .hpa.rule.memory) }}
        {{- include "atapp.logic.hpa_policy.yaml" (dict "autoscaling" .autoscaling "metrics_name" (.hpa.rule.memory.metrics_name | default "atframework_hpa_rule_memory") "policy" .hpa.rule.memory) | nindent 6 }}
        {{- if empty .hpa.rule.memory.metrics_unit }}
      metrics_unit: "bytes"
        {{- end }}
        {{- if empty .hpa.rule.memory.metrics_description }}
      metrics_description: "Resident set size (RSS) for the current process."
        {{- end }}
      {{- else }}
      metrics_name: "atframework_hpa_rule_memory"
      metrics_unit: "bytes"
      metrics_description: "Resident set size (RSS) for the current process."
      {{- end }}
    recent_max_task_count:
      {{- if not (empty .hpa.rule.recent_max_task_count) }}
        {{- include "atapp.logic.hpa_policy.yaml" (dict "autoscaling" .autoscaling "metrics_name" (.hpa.rule.recent_max_task_count.metrics_name | default "atframework_hpa_rule_recent_max_task_count") "policy" .hpa.rule.recent_max_task_count) | nindent 6 }}
        {{- if empty .hpa.rule.recent_max_task_count.metrics_description }}
      metrics_description: "Recent max task count for the current process."
        {{- end }}
      {{- else }}
      metrics_name: "atframework_hpa_rule_recent_max_task_count"
      metrics_description: "Recent max task count for the current process."
      {{- end }}
    controller_status:
      {{- if not (empty .hpa.rule.controller_status) }}
        {{- include "atapp.logic.hpa_policy.yaml" (dict "autoscaling" .autoscaling "metrics_name" (.hpa.rule.controller_status.metrics_name | default "atframework_hpa_rule_controller_status") "policy" .hpa.rule.controller_status) | nindent 6 }}
        {{- if empty .hpa.rule.controller_status.metrics_description }}
      metrics_description: "HPA controller status of the current process."
        {{- end }}
      {{- else }}
      metrics_name: "atframework_hpa_rule_controller_status"
      metrics_description: "HPA controller status of the current process."
      {{- end }}
      {{- if not (empty .hpa.rule.custom) }}
    custom:
        {{- range $_, $hpa_custom_policy := .hpa.rule.custom }}
      - {{ include "atapp.logic.hpa_policy.yaml" (dict "autoscaling" .autoscaling "metrics_name" $hpa_custom_policy.metrics_name "policy" $hpa_custom_policy) | indent 8 | trim }}
        {{- end }}
      {{- end }}
    {{- else }}
    set_scaling_label: true
    cpu_permillage:
      metrics_name: "atframework_hpa_rule_cpu_permillage"
    main_thread_cpu_permillage:
      metrics_name: "atframework_hpa_rule_main_thread_cpu_permillage"
    memory:
      metrics_name: "atframework_hpa_rule_memory"
    recent_max_task_count:
      metrics_name: "atframework_hpa_recent_max_task_count"
    controller_status:
      metrics_name: "atframework_hpa_controller_status"
    {{- end }}
  {{- end }}
{{- end }}
