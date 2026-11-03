#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$PublishDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))

$AtdtoolName = if ($IsWindows) { 'atdtool.exe' } else { 'atdtool' }
$AtdtoolExecutable = Join-Path $PublishDir "tools/atdtool/$AtdtoolName"
if (-not (Test-Path -LiteralPath $AtdtoolExecutable -PathType Leaf)) {
    throw "atdtool was not found at $AtdtoolExecutable. Run this script from the built publish directory."
}
# Refresh packaged library charts before atdtool loads the service dependencies.
& (Join-Path $PSScriptRoot 'update_dependency.ps1')
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$ValuesPaths = (Join-Path $PublishDir 'cloud-native/values/default') + ',' +
    (Join-Path $PublishDir 'cloud-native/values/dev') + ',' +
    (Join-Path $PublishDir 'cloud-native/values/personal')

& $AtdtoolExecutable template (Join-Path $PublishDir 'cloud-native/charts') `
    -o $PublishDir `
    --values $ValuesPaths --set global.world_id=1
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Render start_all/stop_all/kill_all scripts from the tools chart.
# The instance batches come from deploy.yaml (group ordering) and the
# script templates are selected per platform by the caller. kill_all only
# considers service bin directories listed in deploy.yaml.
& $AtdtoolExecutable template (Join-Path $PublishDir 'cloud-native/charts') `
    -o $PublishDir `
    --values $ValuesPaths --set global.world_id=1 --mode deploy_script `
    --scripts 'tools/start_all.ps1.tpl,tools/stop_all.ps1.tpl,tools/kill_all.ps1.tpl'
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $AtdtoolExecutable template (Join-Path $PublishDir 'cloud-native/charts/robot') `
    -o (Join-Path $PublishDir 'robot') `
    --values $ValuesPaths --mode nondeploy
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $AtdtoolExecutable template (Join-Path $PublishDir 'cloud-native/charts/otelcol') `
    -o (Join-Path $PublishDir 'otelcol') `
    --values $ValuesPaths --mode nondeploy
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
exit 0
