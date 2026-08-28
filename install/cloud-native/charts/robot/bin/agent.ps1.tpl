{{- $bus_addr := include "libapp.busAddr" . -}}
$ErrorActionPreference = 'Stop'

& .\robot.exe -mode agent -config ../cfg/robot.yaml @args
exit $LASTEXITCODE
