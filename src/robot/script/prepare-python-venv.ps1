#Requires -Version 7.0
param(
  [Parameter(Mandatory = $true)]
  [string]$PythonCmd,

  [Parameter(Mandatory = $true)]
  [string]$PythonVenv,

  [Parameter(Mandatory = $false)]
  [string]$RequirementsFile
)

$ErrorActionPreference = 'Stop'

$venv = [IO.Path]::GetFullPath($PythonVenv)
if ([string]::IsNullOrWhiteSpace($PythonVenv) -or $venv -eq [IO.Path]::GetPathRoot($venv)) {
  Write-Error "Refusing unsafe venv dir: $PythonVenv"
}

# Recreate the venv when a previous interrupted run left a broken one (no usable pip).
if (Test-Path -LiteralPath $venv) {
  if (-not (Test-Path -LiteralPath $venv -PathType Container)) {
    Write-Error "PythonVenv exists but is not a directory: $PythonVenv"
  }

  $venvPython = Join-Path $venv 'Scripts/python.exe'
  $venvUsable = $false
  if (Test-Path -LiteralPath $venvPython -PathType Leaf) {
    try {
      & $venvPython -m pip --version 2>$null | Out-Null
      $venvUsable = $LASTEXITCODE -eq 0
    } catch {
      $venvUsable = $false
    }
  }

  if (-not $venvUsable) {
    Write-Output 'Removing broken Python venv...'
    Remove-Item -LiteralPath $venv -Recurse -Force
  }
}

if (-not (Test-Path -LiteralPath $venv)) {
  Write-Output "Creating Python venv at $venv..."
  & $PythonCmd -m venv $venv
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  Write-Output 'venv created successfully'
} else {
  Write-Output "Python venv already exists at $venv"
}

if ($RequirementsFile) {
  & (Join-Path $venv 'Scripts/python.exe') -m pip install -r $RequirementsFile
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
