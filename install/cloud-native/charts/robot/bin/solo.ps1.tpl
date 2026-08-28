{{- $bus_addr := include "libapp.busAddr" . -}}
$ErrorActionPreference = 'Stop'

& .\robot.exe -mode solo -config ../cfg/robot.yaml -case_file @args
exit $LASTEXITCODE
