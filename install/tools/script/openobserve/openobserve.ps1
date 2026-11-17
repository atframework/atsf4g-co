# Copyright 2026 atframework
# Manage a single-node OpenObserve OSS container for local development.
# The data directory, generated password, and telemetry values survive start/stop.
param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('download', 'start', 'stop', 'kill', 'cleanup', 'status')]
  [string]$Command,

  [string]$WorkDir = '',
  [ValidateRange(1, 65535)][int]$HttpPort = 5080,
  [ValidateRange(1, 65535)][int]$GrpcPort = 5081,
  [string]$Image = 'public.ecr.aws/zinclabs/openobserve:v1.1.2',
  [string]$ContainerEngine = '',
  [string]$Memory = '3g',
  [string]$Cpus = '2',
  [ValidatePattern('^[A-Za-z0-9_]+$')][string]$Organization = 'atsf4g',
  [string]$RootUserEmail = 'admin@owent.net',
  [ValidateRange(3, 3650)][int]$RetentionDays = 14,
  [ValidateRange(1, 3600)][double]$StartupTimeout = 120
)

$PSDefaultParameterValues['*:Encoding'] = 'UTF8'
$OutputEncoding = [System.Text.UTF8Encoding]::new()
$ErrorActionPreference = 'Stop'
$IsWindowsHost = ($env:OS -eq 'Windows_NT')
if ([string]::IsNullOrEmpty($WorkDir)) {
  $WorkDir = Join-Path $PSScriptRoot '../var/local-test-env/openobserve'
}
$WorkDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($WorkDir)
$DataDir = Join-Path $WorkDir 'data'
$PasswordFile = Join-Path $WorkDir 'openobserve-root-password.txt'
$OtelValuesFile = Join-Path $WorkDir 'values/modules/telemetry.yaml'
$CIDFile = Join-Path $WorkDir 'openobserve.cid'
$EngineFile = Join-Path $WorkDir 'container-engine'
$Initializer = Join-Path $PSScriptRoot 'init_openobserve.py'
$ContainerName = 'atsf4g-openobserve-local'

if ($Command -ne 'start' -and $Command -ne 'download' -and
    [string]::IsNullOrEmpty($ContainerEngine) -and (Test-Path -LiteralPath $EngineFile -PathType Leaf)) {
  $ContainerEngine = (Get-Content -LiteralPath $EngineFile -Raw).Trim()
}

function Find-Python {
  foreach ($candidate in @('python3', 'python')) {
    $command = Get-Command -Name $candidate -ErrorAction SilentlyContinue
    if ($null -eq $command) {
      continue
    }
    & $command.Source -c 'import sys; raise SystemExit(sys.version_info < (3, 8))' 2>$null
    if ($LASTEXITCODE -eq 0) {
      return $command.Source
    }
  }
  Write-Error 'Python 3.8+ is required to prepare credentials and initialize OpenObserve.'
  exit 1
}

function Find-ContainerEngine {
  if (-not [string]::IsNullOrEmpty($ContainerEngine)) {
    $command = Get-Command -Name $ContainerEngine -ErrorAction SilentlyContinue
    if ($null -ne $command) {
      return $command.Source
    }
    return $null
  }
  foreach ($candidate in @('docker', 'podman', 'nerdctl')) {
    $command = Get-Command -Name $candidate -ErrorAction SilentlyContinue
    if ($null -ne $command) {
      return $command.Source
    }
  }
  return $null
}

function Get-ContainerEngine {
  $engine = Find-ContainerEngine
  if ($null -eq $engine) {
    if (-not [string]::IsNullOrEmpty($ContainerEngine)) {
      Write-Error "Container engine '$ContainerEngine' was requested but not found in PATH."
    }
    else {
      Write-Error 'No container engine found. Install docker, podman, or nerdctl first.'
    }
    exit 1
  }
  return $engine
}

function Assert-ContainerEngineReady {
  param([string]$Engine)
  & $Engine info 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Error ("Container engine '$Engine' is installed but its daemon is not reachable. " +
      "Start Docker Desktop, 'podman machine start', or the docker/containerd service first.")
    exit 1
  }
}

function Test-ContainerExists {
  param([string]$Engine)
  & $Engine inspect $ContainerName 2>$null | Out-Null
  return ($LASTEXITCODE -eq 0)
}

function Test-ContainerRunning {
  param([string]$Engine)
  $state = & $Engine inspect -f '{{.State.Running}}' $ContainerName 2>$null
  return ($state -eq 'true')
}

function Invoke-Download {
  $engine = Get-ContainerEngine
  Assert-ContainerEngineReady $engine
  & $engine image inspect $Image 2>$null | Out-Null
  if ($LASTEXITCODE -eq 0) {
    Write-Host "OpenObserve image $Image already exists, skipping pull."
    return
  }
  Write-Host "Pulling $Image ..."
  & $engine pull $Image
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to pull $Image"
    exit 1
  }
}

function Invoke-Prepare {
  $python = Find-Python
  New-Item -Path $DataDir -ItemType Directory -Force | Out-Null
  $passwordOutput = @(& $python $Initializer prepare `
      --password-file $PasswordFile `
      --values-file $OtelValuesFile `
      --root-user-email $RootUserEmail `
      --organization $Organization `
      --http-port $HttpPort)
  if ($LASTEXITCODE -ne 0) {
    Write-Error 'Failed to prepare the OpenObserve password and telemetry values.'
    exit 1
  }
  return $passwordOutput[-1].ToString().Trim()
}

function Invoke-Start {
  $rootPassword = Invoke-Prepare
  $engine = Get-ContainerEngine
  Assert-ContainerEngineReady $engine
  & $engine image inspect $Image 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Host "Pulling $Image ..."
    & $engine pull $Image
    if ($LASTEXITCODE -ne 0) {
      Write-Error "Failed to pull $Image"
      exit 1
    }
  }

  if (Test-ContainerExists $engine) {
    if (Test-ContainerRunning $engine) {
      Write-Host "OpenObserve container $ContainerName is already running. Stopping first..."
      Invoke-Stop
    }
    else {
      & $engine rm $ContainerName 2>$null | Out-Null
    }
  }

  $volumeSuffix = ''
  if (-not $IsWindowsHost -and -not $IsMacOS -and
      (Test-Path -LiteralPath '/sys/fs/selinux/enforce' -PathType Leaf)) {
    if ((Get-Content -LiteralPath '/sys/fs/selinux/enforce' -Raw).Trim() -eq '1') {
      $volumeSuffix = ':Z'
    }
  }
  $containerDataDir = $DataDir.Replace('\', '/')
  $volume = "${containerDataDir}:/data${volumeSuffix}"
  Write-Host "Starting OpenObserve container $ContainerName on 127.0.0.1:${HttpPort}..."
  $runArgs = @(
    'run', '-d', '--name', $ContainerName,
    '--publish', "127.0.0.1:${HttpPort}:5080",
    '--publish', "127.0.0.1:${GrpcPort}:5081",
    '--volume', $volume,
    '--memory', $Memory,
    '--cpus', $Cpus,
    '--env', 'ZO_DATA_DIR=/data',
    '--env', 'ZO_LOCAL_MODE=true',
    '--env', 'ZO_LOCAL_MODE_STORAGE=disk',
    '--env', "ZO_ROOT_USER_EMAIL=${RootUserEmail}",
    '--env', "ZO_ROOT_USER_PASSWORD=${rootPassword}",
    '--env', "ZO_WEB_URL=http://127.0.0.1:${HttpPort}",
    '--env', "ZO_COMPACT_DATA_RETENTION_DAYS=${RetentionDays}",
    '--env', 'ZO_CREATE_ORG_THROUGH_INGESTION=false',
    '--env', 'ZO_TELEMETRY=false',
    '--env', 'ZO_MEM_TABLE_MAX_SIZE=512',
    '--env', 'ZO_MAX_FILE_SIZE_IN_MEMORY=64',
    '--env', 'ZO_MEMORY_CACHE_MAX_SIZE=256',
    '--env', 'ZO_MEMORY_CACHE_DATAFUSION_MAX_SIZE=512',
    '--env', 'ZO_DISK_CACHE_MAX_SIZE=4096',
    '--env', 'ZO_COMPACT_FAST_MODE=false',
    '--env', 'ZO_COMPACT_MAX_FILE_SIZE=128',
    '--env', 'ZO_HTTP_WORKER_NUM=2',
    '--env', 'ZO_FILE_MOVE_THREAD_NUM=1',
    '--env', 'ZO_MEM_DUMP_THREAD_NUM=1',
    '--env', 'ZO_FILE_MERGE_THREAD_NUM=1',
    $Image
  )
  $cid = & $engine @runArgs
  if ($LASTEXITCODE -ne 0) {
    Write-Error 'Failed to start the OpenObserve container.'
    exit 1
  }
  Set-Content -LiteralPath $CIDFile -Value "$cid"
  Set-Content -LiteralPath $EngineFile -Value $engine

  $python = Find-Python
  & $python $Initializer initialize `
    --password-file $PasswordFile `
    --root-user-email $RootUserEmail `
    --organization $Organization `
    --base-url "http://127.0.0.1:${HttpPort}" `
    --values-file $OtelValuesFile `
    --http-port $HttpPort `
    --startup-timeout $StartupTimeout
  if ($LASTEXITCODE -ne 0) {
    Write-Host "OpenObserve initialization failed. Check '$engine logs $ContainerName' for details."
    Invoke-Stop
    exit 1
  }

  Write-Host "OpenObserve is ready at http://127.0.0.1:${HttpPort} (organization: $Organization)."
  Write-Host "  Root user: $RootUserEmail"
  Write-Host "  Password file: $PasswordFile"
  Write-Host "  Persistent data: $DataDir"
  Write-Host "  Telemetry values: $OtelValuesFile"
  Write-Host '  Streams: atframework_traces, atframework_metrics (including spanmetrics), atframework_logs'
}

function Invoke-Stop {
  $engine = Find-ContainerEngine
  if ($null -eq $engine) {
    if (Test-Path -LiteralPath $CIDFile -PathType Leaf) {
      Write-Error "No container engine available to stop $ContainerName (state file: $CIDFile)."
      exit 1
    }
    Write-Host 'No container state found. OpenObserve may not be running.'
    return
  }
  if (-not (Test-ContainerExists $engine)) {
    Write-Host "OpenObserve container $ContainerName does not exist."
    Remove-Item -LiteralPath $CIDFile -Force -ErrorAction SilentlyContinue
    return
  }
  if (Test-ContainerRunning $engine) {
    Write-Host "Stopping OpenObserve container $ContainerName..."
    & $engine stop $ContainerName 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Write-Error "Failed to stop OpenObserve container $ContainerName."
      exit 1
    }
  }
  & $engine rm $ContainerName 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to remove OpenObserve container $ContainerName."
    exit 1
  }
  Remove-Item -LiteralPath $CIDFile -Force -ErrorAction SilentlyContinue
  Write-Host 'OpenObserve stopped. Persistent data and credentials were retained.'
}

function Invoke-Cleanup {
  Invoke-Stop
  Write-Host "Removing OpenObserve persistent data, credentials, and generated values from $WorkDir..."
  if (Test-Path -LiteralPath $WorkDir) {
    Remove-Item -LiteralPath $WorkDir -Recurse -Force
  }
  $engine = Find-ContainerEngine
  if ($null -ne $engine) {
    & $engine image inspect $Image 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
      & $engine rmi $Image 2>$null | Out-Null
      if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to remove image $Image (it may be used elsewhere)."
      }
    }
  }
  Write-Host 'OpenObserve cleanup complete.'
}

function Invoke-Status {
  if (-not (Test-Path -LiteralPath $CIDFile -PathType Leaf)) {
    Write-Host 'OpenObserve is not running (no container state file).'
    return
  }
  $engine = Find-ContainerEngine
  if ($null -eq $engine) {
    Write-Host "Container state file exists ($CIDFile) but no container engine is available."
    return
  }
  if (Test-ContainerRunning $engine) {
    Write-Host "OpenObserve is running at http://127.0.0.1:${HttpPort} (container $ContainerName)."
  }
  else {
    Write-Host 'OpenObserve is not running.'
  }
}

switch ($Command) {
  'download' { Invoke-Download }
  'start' { Invoke-Start }
  'stop' { Invoke-Stop }
  'kill' { Invoke-Stop }
  'cleanup' { Invoke-Cleanup }
  'status' { Invoke-Status }
}
exit 0
