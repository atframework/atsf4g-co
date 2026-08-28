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

# Render start_all/stop_all/kill_all scripts from the tools chart.
# The instance batches come from deploy.yaml (group ordering) and the
# script templates are selected per platform by the caller. kill_all only
# considers service bin directories listed in deploy.yaml.
& $AtdtoolExecutable template (Join-Path $PSScriptRoot '..\..\cloud-native\charts') `
    -o (Join-Path $PSScriptRoot '..\..') `
    --values $ValuesPaths --set global.world_id=1 --mode deploy_script `
    --scripts 'tools/start_all.ps1.tpl,tools/stop_all.ps1.tpl,tools/kill_all.ps1.tpl'
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
