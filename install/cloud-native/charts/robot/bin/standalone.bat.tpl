{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robot.exe -config ../cfg/robot.yaml %*
