$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

Set-Location -LiteralPath $PSScriptRoot

pwsh .\etcd\setup-etcd.ps1 stop
pwsh .\redis\redis.ps1 stop

taskkill.exe /F /T /IM otelcol-contrib.exe *> $null
exit $LASTEXITCODE
