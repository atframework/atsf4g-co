{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robot.exe -config ../cfg/robot_{{ $bus_addr }}.yaml %*
