{{- define "atapp.start.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{- $proc_name := .Values.proc_name -}}
{{- $type_name := (.Values.type_name | default (include "libapp.name" .)) -}}

#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

./{{ $proc_name }} --config ../cfg/{{ $type_name }}_{{ $bus_addr }}.yaml -pid ./{{ $type_name }}_{{ $bus_addr }}.pid start
{{- end }}

{{- define "atapp.start.bat" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{- $proc_name := .Values.proc_name -}}
{{- $type_name := (.Values.type_name | default (include "libapp.name" .)) -}}
@echo off
setlocal

cd %cd%

set "DLL_DIR=%~dp0..\..\lib"
set "PATH=%DLL_DIR%;%PATH%"
set "DLL_DIR=%~dp0..\..\bin"
set "PATH=%DLL_DIR%;%PATH%"

.\{{ $proc_name }}.exe --config ..\cfg\{{ $type_name }}_{{ $bus_addr }}.yaml -pid .\{{ $type_name }}_{{ $bus_addr }}.pid start

endlocal
{{- end }}
