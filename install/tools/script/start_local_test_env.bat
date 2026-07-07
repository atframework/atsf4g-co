@echo off
cd /d %~dp0

pwsh .\etcd\setup-etcd.ps1 start
pwsh .\redis\redis.ps1 start