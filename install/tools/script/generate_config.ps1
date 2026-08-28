$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

Set-Location -LiteralPath $PSScriptRoot

$AtdtoolExecutable = Join-Path $PSScriptRoot '..\atdtool\atdtool.exe'
$ValuesPaths = (Join-Path $PSScriptRoot '..\..\cloud-native\values\default') + ',' +
    (Join-Path $PSScriptRoot '..\..\cloud-native\values\dev') + ',' +
    (Join-Path $PSScriptRoot '..\..\cloud-native\values\personal')

& $AtdtoolExecutable template (Join-Path $PSScriptRoot '..\..\cloud-native\charts') `
    -o (Join-Path $PSScriptRoot '..\..') `
    --values $ValuesPaths --set global.world_id=1
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $AtdtoolExecutable template (Join-Path $PSScriptRoot '..\..\cloud-native\charts\robot') `
    -o (Join-Path $PSScriptRoot '..\..\robot') `
    --values $ValuesPaths --mode nondeploy
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $AtdtoolExecutable template (Join-Path $PSScriptRoot '..\..\cloud-native\charts\otelcol') `
    -o (Join-Path $PSScriptRoot '..\..\otelcol') `
    --values $ValuesPaths --mode nondeploy
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
