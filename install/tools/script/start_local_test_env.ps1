$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

Set-Location -LiteralPath $PSScriptRoot

pwsh .\etcd\setup-etcd.ps1 start
pwsh .\redis\redis.ps1 start

$OtelBinDir = Join-Path $PSScriptRoot '..\..\otelcol\bin'
Set-Location -LiteralPath $OtelBinDir
if (-not (Test-Path -LiteralPath (Join-Path $OtelBinDir 'otelcol-contrib.exe') -PathType Leaf)) {
    Write-Host '[ERROR] Otel collector executable does not exist: "otelcol-contrib.exe"'
    exit 1
}

$LogDir = Join-Path $OtelBinDir '..\log'
if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

& .\otelcol-contrib.exe --config=..\cfg\config.yaml
exit $LASTEXITCODE
