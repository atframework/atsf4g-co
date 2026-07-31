{{- $bus_addr := include "libapp.busAddr" . -}}
@echo off

cd %cd%

.\robot.exe -mode agent -config ../cfg/robot.yaml %*
