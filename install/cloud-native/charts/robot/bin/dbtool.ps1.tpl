{{- $bus_addr := include "libapp.busAddr" . -}}
$ErrorActionPreference = 'Stop'

& .\robot.exe -mode dbtool -config ../cfg/robot.yaml @args
exit $LASTEXITCODE
