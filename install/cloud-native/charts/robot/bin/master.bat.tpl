{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robot.exe -mode master -config ../cfg/robot.yaml %*
