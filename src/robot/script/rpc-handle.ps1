#Requires -Version 7.0
param(
  [Parameter(Mandatory = $true)]
  [string]$PythonBin,

  [Parameter(Mandatory = $true)]
  [string]$MakoGeneratorPy,

  [Parameter(Mandatory = $true)]
  [string]$ServerPidFile,

  [Parameter(Mandatory = $true)]
  [string]$ServerPortFile,

  [Parameter(Mandatory = $false)]
  [string]$PackagePrefix,

  [Parameter(Mandatory = $true)]
  [string]$ProjectDir,

  [Parameter(Mandatory = $true)]
  [string]$PbFile,

  [Parameter(Mandatory = $true)]
  [string]$RuleFile,

  [Parameter(Mandatory = $false)]
  [string]$UseBaseInterpreter = 'false'
)

$ErrorActionPreference = 'Stop'

$pythonBin = $PythonBin
if ($UseBaseInterpreter -eq 'true') {
  # The built-in (pinned) mako-generator identifies client/server by realpath(sys.executable),
  # but Python 3.14 venvs on Windows are redirector executables whose real process image is the
  # base interpreter. Run the client with the base interpreter and the venv site-packages on
  # PYTHONPATH so the auto-started server passes the image check.
  $pythonBin = & $PythonBin -c "import sys; print(getattr(sys, '_base_executable', None) or sys.executable)"
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  $venvRoot = Split-Path (Split-Path $PythonBin -Parent) -Parent
  $env:PYTHONPATH = Join-Path $venvRoot 'Lib\site-packages'
}

$generatorArgs = @(
  $MakoGeneratorPy,
  '--server-pid-file', $ServerPidFile,
  '--server-port-file', $ServerPortFile,
  '--server-auto-start',
  '--client-mode'
)
if ($PackagePrefix) {
  $generatorArgs += @('--add-package-prefix', $PackagePrefix)
}
$generatorArgs += @(
  '--project-dir', $ProjectDir,
  '--pb-file', $PbFile,
  '-c', $RuleFile
)

& $pythonBin @generatorArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
