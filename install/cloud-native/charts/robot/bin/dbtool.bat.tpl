{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robotd.exe -mode dbtool -config ../cfg/robot_{{ $bus_addr }}.yaml %*
