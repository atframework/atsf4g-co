{{- define "atapp.stop.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{- $proc_name := .Values.proc_name -}}
{{- $type_name := (.Values.type_name | default (include "libapp.name" .)) -}}

#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

kill $(cat ./{{ $type_name }}_{{ $bus_addr }}.pid)

{{- end }}

{{- define "atapp.stop.bat" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{- $proc_name := .Values.proc_name -}}
{{- $type_name := (.Values.type_name | default (include "libapp.name" .)) -}}
@echo off

cd %cd%

for /F %%j in ( 'type .\{{ $type_name }}_{{ $bus_addr }}.pid' ) do ( set PID=%%j )   
echo PID=!PID!

taskkill /F /PID %PID%

{{- end }}
