{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robot.exe -mode dbtool -config ../cfg/robot_{{ $bus_addr }}.yaml %*
