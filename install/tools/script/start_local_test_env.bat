@echo off
cd /d %~dp0

pwsh .\etcd\setup-etcd.ps1 start
pwsh .\redis\redis.ps1 start

cd /d %~dp0..\..\otelcol\bin
if not exist "otelcol-contrib.exe" (
	echo [ERROR] Otel collector executable does not exist: "otelcol-contrib.exe"
	exit /b 1
)

if not exist ..\log (
    mkdir ..\log
)
".\otelcol-contrib.exe" --config=..\cfg\config.yaml