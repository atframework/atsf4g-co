[CmdletBinding(PositionalBinding = $false)]
param(
  [Parameter(Mandatory = $false)]
  [string]$BuildDir = "${PSScriptRoot}\..\build_jobs_cmake_tools",

  [Parameter(Mandatory = $false)]
  [string]$Config = "Debug",

  [Parameter(Mandatory = $false)]
  [string]$Target = "atbus_unit_test",

  # Extra native build tool args passed after `--` (e.g. -v for Ninja)
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$NativeArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Import-CmdEnvironment {
  param(
    [Parameter(Mandatory = $true)]
    [string]$BatchFile,

    [Parameter(Mandatory = $false)]
    [string]$BatchArgs = ''
  )

  if (-not (Test-Path -LiteralPath $BatchFile)) {
    return $false
  }

  $cmd = if ([string]::IsNullOrWhiteSpace($BatchArgs)) {
    "call `"$BatchFile`" >nul && set"
  }
  else {
    "call `"$BatchFile`" $BatchArgs >nul && set"
  }

  $envDump = & cmd.exe /d /s /c $cmd
  if ($LASTEXITCODE -ne 0 -or -not $envDump) {
    return $false
  }

  foreach ($line in $envDump) {
    if ($line -match '^(.*?)=(.*)$') {
      [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
  }

  return $true
}

function Ensure-MsvcBuildEnvironment {
  if (-not [string]::IsNullOrWhiteSpace($env:VCToolsInstallDir) -and -not [string]::IsNullOrWhiteSpace($env:INCLUDE)) {
    return
  }

  $vcvarsCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\17\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\17\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\17\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
  )

  foreach ($vcvars in $vcvarsCandidates) {
    if (Import-CmdEnvironment -BatchFile $vcvars) {
      Write-Host "[cmake-build-utf8] Imported MSVC environment from: $vcvars" -ForegroundColor DarkCyan
      return
    }
  }

  Write-Warning '[cmake-build-utf8] MSVC environment variables are missing and vcvars64.bat was not found. Build may fail.'
}

# Ensure UTF-8 console encoding so MSBuild/Ninja/CL diagnostics are readable.
try { chcp 65001 > $null } catch {}
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

Ensure-MsvcBuildEnvironment

$resolvedBuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$cmakeArgs = @(
  '--build', $resolvedBuildDir,
  '--config', $Config,
  '--target', $Target
)

if ($null -ne $NativeArgs -and $NativeArgs.Count -gt 0) {
  $cmakeArgs += '--'
  $cmakeArgs += $NativeArgs
}

Write-Host "[cmake-build-utf8] BuildDir=$resolvedBuildDir Config=$Config Target=$Target" -ForegroundColor Cyan

& cmake @cmakeArgs
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
  exit $exitCode
}
