$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

Set-Location -LiteralPath $PSScriptRoot

$HelmDir = Join-Path $PSScriptRoot '..\..\tools\helm'
$env:PATH = "$HelmDir;$env:PATH"

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
