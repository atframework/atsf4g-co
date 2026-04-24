{{/*
Expand the name of the chart.
*/}}
{{- define "libapp.name" -}}
  {{- default (default .Chart.Name .Values.nameOverride) .Values.type_name | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
We truncate at 63 chars because some Kubernetes name fields are limited to this (by the DNS naming spec).
If release name contains chart name it will be used as a full name.
*/}}
{{- define "libapp.fullname" -}}
  {{- if .Values.fullnameOverride }}
    {{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
  {{- else }}
    {{- $name := default .Chart.Name .Values.nameOverride }}
    {{- if contains $name .Release.Name }}
      {{- .Release.Name | trunc 63 | trimSuffix "-" }}
    {{- else }}
      {{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
    {{- end }}
  {{- end }}
{{- end }}

{{- define "libapp.discovery_name" -}}
  {{- default (include "libapp.name" .) .Values.discovery_name | trunc 63 | trimSuffix "-" }}
{{- end }}

{{- define "libapp.hpa_target_name" -}}
  {{- if not .Values.tcm_mode }}
    {{- include "libapp.fullname" . -}}
  {{- else }}
    {{- if not (empty .Values.atapp.deployment.deployment_environment) -}}
      {{ .Values.atapp.deployment.deployment_environment }}-{{- include "libapp.name" . -}}
    {{- else }}
      {{- include "libapp.name" . -}}
    {{- end }}
  {{- end }}
{{- end }}

{{- define "libapp.metrics.normalize_prometheus_name" -}}
  {{- if and .metrics_name .metrics_unit -}}
    {{- if hasSuffix .metrics_unit .metrics_name -}}
      {{ regexReplaceAll "[^a-zA-Z0-9_:]" .metrics_name "_" }}
    {{- else -}}
      {{ regexReplaceAll "[^a-zA-Z0-9_:]" .metrics_name "_" }}_{{ regexReplaceAll "[^a-zA-Z0-9_:]" .metrics_unit "_" }}
    {{- end -}}
  {{- else -}}
    {{ regexReplaceAll "[^a-zA-Z0-9_:]" .metrics_name "_" }}
  {{- end -}}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "libapp.chart" -}}
  {{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "libapp.labels" -}}
helm.sh/chart: {{ include "libapp.chart" . }}
{{ include "libapp.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels
*/}}
{{- define "libapp.selectorLabels" -}}
app.kubernetes.io/name: {{ include "libapp.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.yygf.io/service-type: gs
app.yygf.io/id: {{ include "libapp.logicID" . | quote }}
app.yygf.io/name: {{ include "libapp.name" . }}
app.yygf.io/environment: {{ include "libapp.environment" . }}
app.yygf.io/partition: {{ .Values.partition | quote }}
{{- end }}

{{/*
Create the name of the service account to use
*/}}
{{- define "libapp.serviceAccountName" -}}
{{- if .Values.serviceAccount.create }}
{{- default (include "libapp.fullname" .) .Values.serviceAccount.name }}
{{- else }}
{{- default "default" .Values.serviceAccount.name }}
{{- end }}
{{- end }}

{{/*
Libapp deploy environment
*/}}
{{- define "libapp.environment" -}}
{{- default "production" .Values.envOverride }}
{{- end }}

{{/*
Libapp deploy cluster
*/}}
{{- define "libapp.cluster" -}}
{{- default "local" .Values.cluster }}
{{- end }}

{{/*
Calculate LogicID from world_id and zone_id
Formula: worldID * ZoneBase() + zoneID
If .Values.logic_id is set, use it directly
*/}}
{{- define "libapp.logicID" -}}
  {{- if .Values.logic_id -}}
    {{- .Values.logic_id -}}
  {{- else -}}
    {{- $worldID := .Values.world_id | default 1 | toString | atoi -}}
    {{- $zoneID := .Values.zone_id | default 1 | toString | atoi -}}
    {{- $idMaskParts := splitList "." (.Values.id_mask | default "24.12.12.16" | toString) -}}
    {{- $zoneBits := 12 -}}
    {{- if ge (len $idMaskParts) 2 -}}
      {{- $zoneBits = (index $idMaskParts 1) | atoi -}}
    {{- end -}}
    {{- /* Calculate 2^zoneBits */ -}}
    {{- $maxVal := 1 -}}
    {{- range until $zoneBits -}}
      {{- $maxVal = mul $maxVal 2 -}}
    {{- end -}}
    {{- /* Find minimum 10^n > maxVal */ -}}
    {{- $base := 1 -}}
    {{- range until 100 -}}
      {{- if le $base $maxVal -}}
        {{- $base = mul $base 10 -}}
      {{- end -}}
    {{- end -}}
    {{- add (mul $worldID $base) $zoneID -}}
  {{- end -}}
{{- end }}

{{- define "libapp.busAddr" -}}
  {{- if .Values.bus_addr -}}
    {{- .Values.bus_addr -}}
  {{- else -}}
    ${ATAPP_INSTANCE_ID}
  {{- end -}}
{{- end }}

{{- define "libapp.configure.hostname" -}}
  {{- if .Values.external_ipv6 -}}
    {{- .Values.external_ipv6 -}}
  {{- else if .Values.external_ipv4 -}}
    {{- .Values.external_ipv4 -}}
  {{- else if .Values.internal_ipv6 -}}
    {{- .Values.internal_ipv6 -}}
  {{- else if .Values.internal_ipv4 -}}
    {{- .Values.internal_ipv4 -}}
  {{- end -}}
{{- end }}

{{- define "libapp.atbus.calculateAtproxyPort" -}}
  {{- $basePort := (dig "policy" "port" "atproxy_base" 7100 .Values.atapp.atbus ) -}}
  {{- $machineIndex := (dig "policy" "port" "machine_index" 0 .Values.atapp.atbus ) -}}
  {{- $machineIndexMultiply := (dig "policy" "port" "machine_index_multiply" 500 .Values.atapp.atbus ) -}}
  {{- $insID := (1 | int64) -}}
  {{- if eq .Values.type_name "atproxy" -}}
    {{- $insID = (.Values.instance_id | default 1 | int64) -}}
  {{- else -}}
    {{- $insID = (1 | int64) -}}
  {{- end -}}
  {{- add $basePort (mul $machineIndex $machineIndexMultiply) $insID -}}
{{- end }}

{{- define "libapp.atbus.calculateServicePort" -}}
  {{- $basePort := (1 | int64) -}}
  {{- $machineIndex := (dig "policy" "port" "machine_index" 0 .Values.atapp.atbus ) -}}
  {{- $machineIndexMultiply := (dig "policy" "port" "machine_index_multiply" 500 .Values.atapp.atbus ) -}}
  {{- $typeMultiply := (dig "policy" "port" "type_multiply" 10 .Values.atapp.atbus ) -}}
  {{- $typeID := .Values.type_id | default 11 | int64 -}}
  {{- $insID := .Values.instance_id | default 1 | int64 -}}
  {{- if eq .Values.type_name "atproxy" }}
    {{- $basePort = (dig "policy" "port" "atproxy_base" 7100 .Values.atapp.atbus | int) -}}
    {{- add $basePort (mul $machineIndex $machineIndexMultiply) $insID -}}
  {{- else }}
    {{- $basePort = (dig "policy" "port" "base" 7200 .Values.atapp.atbus | int) -}}
    {{- add $basePort (mul $machineIndex $machineIndexMultiply) (mul $typeID $typeMultiply) $insID -}}
  {{- end }}
{{- end }}
