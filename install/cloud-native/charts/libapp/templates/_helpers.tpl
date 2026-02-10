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
Calculate ZoneBase from bus_addr_template
Extracts zone bits from bus_addr_template (e.g., "world:12.zone:12" -> 12)
ZoneBase finds minimum 10^n where 10^n > 2^zoneBits
*/}}
{{- define "libapp.zoneBase" -}}
  {{- $busAddrTemplate := .Values.bus_addr_template | default "world:12.zone:12.function:16.instance:24" -}}
  {{- $zonePart := (split ".zone:" $busAddrTemplate)._1 -}}
  {{- $zoneBits := (split "." $zonePart)._0 | atoi -}}
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
  {{- $base | toString -}}
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
    {{- $worldID := .Values.world_id | default 1 | toString | int64 -}}
    {{- $zoneID := .Values.zone_id | default 1 | toString | int64 -}}
    {{- $busAddrTemplate := .Values.bus_addr_template | default "world:12.zone:12.function:16.instance:24" -}}
    {{- $zonePart := (split ".zone:" $busAddrTemplate)._1 -}}
    {{- $zoneBits := (split "." $zonePart)._0 | atoi -}}
    {{- /* Calculate 2^zoneBits */ -}}
    {{- $maxVal := (1 | int64) -}}
    {{- range until $zoneBits -}}
      {{- $maxVal = mul $maxVal 2 -}}
    {{- end -}}
    {{- /* Find minimum 10^n > maxVal */ -}}
    {{- $base := (1 | int64) -}}
    {{- range until 100 -}}
      {{- if le $base $maxVal -}}
        {{- $base = mul $base 10 -}}
      {{- end -}}
    {{- end -}}
    {{- add (mul $worldID $base) $zoneID -}}
  {{- end -}}
{{- end }}

{{/*
Calculate BusAddr from world_id, zone_id, type_id
Formula: worldID.zoneID(or 0 if world_instance).typeID.insID(fixed 1)
If .Values.bus_addr is set, use it directly
*/}}
{{- define "libapp.busAddr" -}}
  {{- if .Values.bus_addr -}}
    {{- .Values.bus_addr -}}
  {{- else -}}
    {{- $worldID := .Values.world_id | default 1 | toString -}}
    {{- $zoneID := .Values.zone_id | default 1 | toString -}}
    {{- $isWorldInstance := .Values.world_instance | default false -}}
    {{- $typeID := .Values.type_id | default 11 | toString -}}
    {{- $insID := .Values.instance_id | default 1 | toString -}}
    {{- $zoneIDPart := $zoneID -}}
    {{- if $isWorldInstance -}}
      {{- $zoneIDPart = "0" -}}
    {{- end -}}
    {{- printf "%s.%s.%s.%s" $worldID $zoneIDPart $typeID $insID -}}
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
  {{- $basePort := (dig .Values.atapp.atbus "policy" "port" "atproxy_base" 7100 | int) -}}
  {{- $machineIndex := (dig .Values.atapp.atbus "policy" "port" "machine_index" 0 | int64) -}}
  {{- $machineIndexMultiply := (dig .Values.atapp.atbus "policy" "port" "machine_index_multiply" 500 | int64) -}}
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
  {{- $machineIndex := (dig .Values.atapp.atbus "policy" "port" "machine_index" 0 | int64) -}}
  {{- $machineIndexMultiply := (dig .Values.atapp.atbus "policy" "port" "machine_index_multiply" 500 | int64) -}}
  {{- $typeMultiply := (dig .Values.atapp.atbus "policy" "port" "type_multiply" 10 | int64) -}}
  {{- $typeID := .Values.type_id | default 11 | int64 -}}
  {{- $insID := .Values.instance_id | default 1 | int64 -}}
  {{- if eq .Values.type_name "atproxy" }}
    {{- $basePort = (dig .Values.atapp.atbus "policy" "port" "atproxy_base" 7100 | int) -}}
  {{- else }}
    {{- $basePort = (dig .Values.atapp.atbus "policy" "port" "base" 7200 | int) -}}
  {{- end }}
  {{- add $basePort (mul $machineIndex $machineIndexMultiply) (mul $typeID $typeMultiply) $insID -}}
{{- end }}
