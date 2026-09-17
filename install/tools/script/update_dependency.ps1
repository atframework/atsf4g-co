$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

Set-Location -LiteralPath $PSScriptRoot

# helm is installed by the CMake build into <build>/tools/helm, next to the deploy tree (<build>/publish).
# Probe that location first, then the legacy in-tree <publish>/tools/helm for older build trees.
$HelmDirCandidates = @(
    (Join-Path $PSScriptRoot '..\..\..\tools\helm'),
    (Join-Path $PSScriptRoot '..\..\tools\helm')
)
foreach ($HelmDirCandidate in $HelmDirCandidates) {
    if (Test-Path -LiteralPath $HelmDirCandidate -PathType Container) {
        $env:PATH = "$HelmDirCandidate;$env:PATH"
        break
    }
}

if (-not (Get-Command helm -ErrorAction SilentlyContinue)) {
    Write-Host '[ERROR] helm not found. Expected under <build>/tools/helm or on PATH.'
    exit 1
}

$Failed = $false
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '..\..\cloud-native\charts') -Directory |
    ForEach-Object {
        if ($_.Name -ne 'libapp' -and $_.Name -ne 'app') {
            Write-Host $_.FullName
            helm dependency update $_.FullName
            if ($LASTEXITCODE -ne 0) {
                $Failed = $true
            }
        }
    }

if ($Failed) {
    exit 1
}
