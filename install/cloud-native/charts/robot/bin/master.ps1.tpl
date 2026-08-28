{{- $bus_addr := include "libapp.busAddr" . -}}
$ErrorActionPreference = 'Stop'

& .\robot.exe -mode master -config ../cfg/robot.yaml @args
exit $LASTEXITCODE
