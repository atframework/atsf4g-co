{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robotd.exe -mode solo -config ../cfg/robot_{{ $bus_addr }}.yaml -case_file %*
