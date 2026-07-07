@echo off
cd /d %~dp0

pwsh .\etcd\setup-etcd.ps1 stop
pwsh .\redis\redis.ps1 stop