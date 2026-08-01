#Requires -Version 7.0
param(
  [Parameter(Mandatory = $true)]
  [string]$PythonVenv,

  [Parameter(Mandatory = $true)]
  [string]$BuildSettingBin,

  [Parameter(Mandatory = $true)]
  [string]$SettingsFile,

  [Parameter(Mandatory = $false)]
  [string]$RequirementsFile
)

$ErrorActionPreference = 'Stop'

$pythonBin = Join-Path $PythonVenv 'Scripts/python.exe'

if ($RequirementsFile) {
  if (Test-Path $RequirementsFile) {
    Write-Output 'Installing Python dependencies...'
    & $pythonBin -m pip install -r $RequirementsFile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Output 'Dependencies installed'
  } else {
    Write-Output "requirements.txt not found at $RequirementsFile"
  }
}

$versionOutput = & $pythonBin --version 2>&1
$version = ("$versionOutput" -replace '^Python ' -replace '[^0-9.]', '')

& $BuildSettingBin set python -path $pythonBin -version $version -settings-file $SettingsFile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Output "Python Version: $version environment OK!"
