{{- $bus_addr := include "libapp.busAddr" . -}}
$ErrorActionPreference = 'Stop'

& .\robot.exe -config ../cfg/robot.yaml @args
exit $LASTEXITCODE
