$ErrorActionPreference = 'Stop'

$OtelcolExecutable = Join-Path $PSScriptRoot 'otelcol-contrib.exe'
if (-not (Test-Path -LiteralPath $OtelcolExecutable -PathType Leaf)) {
    Write-Host ('[ERROR] Otel collector executable does not exist: "{0}"' -f $OtelcolExecutable)
    exit 1
}

& $OtelcolExecutable ("--config={0}" -f (Join-Path $PSScriptRoot '..\cfg\config.yaml'))
exit $LASTEXITCODE
