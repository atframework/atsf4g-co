#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$PublishDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$HelmName = if ($IsWindows) { 'helm.exe' } else { 'helm' }
$HelmExecutable = $null
foreach ($Directory in @((Join-Path $PublishDir '../tools/helm'), (Join-Path $PublishDir 'tools/helm'))) {
    $Candidate = Join-Path $Directory $HelmName
    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        $HelmExecutable = $Candidate
        break
    }
}
if (-not $HelmExecutable) {
    $HelmExecutable = (Get-Command $HelmName -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1).Source
}
if (-not $HelmExecutable) {
    throw 'Helm was not found under <build>/tools/helm, <publish>/tools/helm, or on PATH.'
}

$Failed = $false
foreach ($Chart in Get-ChildItem -LiteralPath (Join-Path $PublishDir 'cloud-native/charts') -Directory) {
    if ($Chart.Name -in 'libapp', 'app' -or
        -not (Test-Path -LiteralPath (Join-Path $Chart.FullName 'Chart.yaml') -PathType Leaf)) {
        continue
    }

    Write-Host $Chart.FullName
    & $HelmExecutable dependency update $Chart.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Helm dependency update failed for $($Chart.Name) (exit $LASTEXITCODE)."
        $Failed = $true
    }
}
exit [int]$Failed
