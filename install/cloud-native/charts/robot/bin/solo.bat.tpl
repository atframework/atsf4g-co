{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robot.exe -mode solo -config ../cfg/robot.yaml -case_file %*
