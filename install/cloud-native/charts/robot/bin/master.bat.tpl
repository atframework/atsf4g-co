{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robotd.exe -mode master -config ../cfg/robot_{{ $bus_addr }}.yaml %*
