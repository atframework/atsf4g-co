# Copyright 2026 atframework
# PowerShell script to download, start, stop, and manage redis for unit testing.
# Works on Windows PowerShell 5.1 and pwsh 7+ on Windows/Linux/macOS.
#   - Windows: downloads the tporadowski redis port (same as before).
#   - Linux/macOS: redis has no official binary release, so the Docker Official
#     Image is used through a local container engine (docker, podman or nerdctl,
#     auto-detected in this order). No root or system packages are required.
# Usage: redis.ps1 -Command <download|start|stop|cleanup|status>
#   -WorkDir DIR          Working directory (default: <temp dir>/redis-unit-test)
#   -ClientPort PORT      Listening port (default: 6379)
#   -RedisVersion VER     Image tag to use on Linux/macOS (default: 7.2.16,
#                         the last BSD-3-Clause release line; ignored on Windows)
#   -ContainerEngine ENG  Container engine to use on Linux/macOS
#                         (docker|podman|nerdctl; default: auto-detect)

param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("download", "start", "stop", "cleanup", "status")]
  [string]$Command,

  [string]$WorkDir = "",

  [int]$ClientPort = 6379,

  [string]$RedisVersion = "latest",

  [string]$ContainerEngine = ""
)

$PSDefaultParameterValues['*:Encoding'] = 'UTF8'
$OutputEncoding = [System.Text.UTF8Encoding]::new()
$ErrorActionPreference = "Stop"

# $IsWindows/$IsMacOS only exist on PowerShell 7; $env:OS is 'Windows_NT' on every Windows
# host (5.1 and 7+), so it is used as the 5.1 compatible probe.
$IsWindowsHost = ($env:OS -eq 'Windows_NT')
$HostOs = "linux"
if ($IsWindowsHost) {
  $HostOs = "windows"
}
elseif ($IsMacOS) {
  $HostOs = "darwin"
}

if ([string]::IsNullOrEmpty($WorkDir)) {
  if (Test-Path ENV:WORK_DIR) {
    $WorkDir = Join-Path $env:WORK_DIR "redis-unit-test"
  }
  else {
    $WorkDir = Join-Path (Get-Location) "redis-unit-test"
  }
}

# Windows (native) paths
$ExeSuffix = ""
if ($IsWindowsHost) {
  $ExeSuffix = ".exe"
}
$REDIS_SERVER_EXE = Join-Path $WorkDir "redis-server$ExeSuffix"
$REDIS_CLI_EXE = Join-Path $WorkDir "redis-cli$ExeSuffix"
$REDIS_CONF = Join-Path $WorkDir "redis.windows.conf"
$PID_FILE = Join-Path $WorkDir "redis.pid"
$LOG_FILE = Join-Path $WorkDir "redis.log"

# Unix (container) state
# 7.2.x is the last BSD-3-Clause licensed release line and resolves "latest".
$ImageTag = $RedisVersion
if ($ImageTag -eq "latest") {
  $ImageTag = "7.2.16"
}
$REDIS_IMAGE = "redis:$ImageTag"
$CONTAINER_NAME = "atsf4g-redis-unit-test"
$CID_FILE = Join-Path $WorkDir "redis.cid"

function Get-RunningProcess {
  # Returns the process for the given PID text, or $null when the text is not a number or
  # the process is gone. Reading a corrupt/partially written PID file must not blow up
  # with a parameter binding error under $ErrorActionPreference = 'Stop'.
  param([string]$PidText)

  $pidInt = $PidText -as [int]
  if ($null -eq $pidInt) {
    return $null
  }
  $proc = Get-Process -Id $pidInt -ErrorAction SilentlyContinue
  if ($null -eq $proc -or $proc.HasExited) {
    return $null
  }
  return $proc
}

function Find-ContainerEngine {
  # Returns the engine command name, or $null when none is available.
  if (-not [string]::IsNullOrEmpty($ContainerEngine)) {
    if ($null -ne (Get-Command -Name $ContainerEngine -ErrorAction SilentlyContinue)) {
      return $ContainerEngine
    }
    Write-Error "Container engine '$ContainerEngine' was requested but not found in PATH."
    exit 1
  }
  foreach ($engine in @("docker", "podman", "nerdctl")) {
    if ($null -ne (Get-Command -Name $engine -ErrorAction SilentlyContinue)) {
      return $engine
    }
  }
  return $null
}

function Get-ContainerEngine {
  $engine = Find-ContainerEngine
  if ($null -eq $engine) {
    Write-Error ("No container engine found. redis runs in a container on Linux/macOS; " +
      "install docker or podman first " +
      "(macOS: 'brew install podman; podman machine init; podman machine start' or Docker Desktop; " +
      "Debian/Ubuntu: 'apt-get install docker.io' or 'apt-get install podman').")
    exit 1
  }
  return $engine
}

function Assert-ContainerEngineReady {
  param([string]$Engine)
  & $Engine info 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Error ("Container engine '$Engine' is installed but its daemon is not reachable. " +
      "Start it first (Docker Desktop on macOS, 'podman machine start', or the docker/containerd service).")
    exit 1
  }
}

function Test-ContainerExists {
  param([string]$Engine)
  & $Engine inspect $CONTAINER_NAME 2>$null | Out-Null
  return ($LASTEXITCODE -eq 0)
}

function Test-ContainerRunning {
  param([string]$Engine)
  $state = & $Engine inspect -f '{{.State.Running}}' $CONTAINER_NAME 2>$null
  return ($state -eq "true")
}

function Invoke-DownloadWindows {
  # Strip the leading 'v' from the tag to match the zip name (e.g. v5.0.14.1 -> 5.0.14.1)
  $downloadUrl = "https://github.com/tporadowski/redis/releases/download/v5.0.14.1/Redis-x64-5.0.14.1.zip"
  $zipFile = Join-Path $WorkDir "redis.zip"

  Write-Host "Downloading Redis for Windows x64..."
  Write-Host "URL: $downloadUrl"

  try {
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipFile -UseBasicParsing
  }
  catch {
    Write-Error "Failed to download Redis: $_"
    exit 1
  }

  Write-Host "Extracting..."
  $extractDir = Join-Path $WorkDir "redis-extract"
  if (Test-Path -LiteralPath $extractDir) {
    Remove-Item -Recurse -Force $extractDir
  }
  Expand-Archive -Path $zipFile -DestinationPath $extractDir -Force

  # Copy the core executables and the default configuration file
  Copy-Item -Path (Join-Path $extractDir "redis-server.exe") -Destination $REDIS_SERVER_EXE -Force
  Copy-Item -Path (Join-Path $extractDir "redis-cli.exe") -Destination $REDIS_CLI_EXE -Force
  if (Test-Path -LiteralPath (Join-Path $extractDir "redis.windows.conf")) {
    Copy-Item -Path (Join-Path $extractDir "redis.windows.conf") -Destination $REDIS_CONF -Force
  }

  # Cleanup temp files
  Remove-Item -Recurse -Force $extractDir
  Remove-Item -Force $zipFile

  Write-Host "Redis downloaded successfully to $WorkDir"
}

function Invoke-DownloadUnix {
  $engine = Get-ContainerEngine
  Assert-ContainerEngineReady $engine

  & $engine image inspect $REDIS_IMAGE 2>$null | Out-Null
  if ($LASTEXITCODE -eq 0) {
    Write-Host "Redis image $REDIS_IMAGE already exists, skipping pull."
    Write-Host "Use 'cleanup' command first if you want to re-pull."
    return
  }

  Write-Host "Pulling $REDIS_IMAGE ..."
  & $engine pull $REDIS_IMAGE
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to pull $REDIS_IMAGE"
    exit 1
  }

  Write-Host "Redis image pulled successfully."
}

function Invoke-Download {
  if ($IsWindowsHost) {
    if ((Test-Path -LiteralPath $REDIS_SERVER_EXE) -and (Test-Path -LiteralPath $REDIS_CLI_EXE)) {
      Write-Host "Redis binaries already exist at $WorkDir, skipping download."
      Write-Host "Use 'cleanup' command first if you want to re-download."
      return
    }
    New-Item -Path $WorkDir -ItemType Directory -Force | Out-Null
    Invoke-DownloadWindows
  }
  else {
    Invoke-DownloadUnix
  }
}

function Invoke-StartWindows {
  if (!(Test-Path -LiteralPath $REDIS_SERVER_EXE)) {
    Write-Host "Redis binary not found. Downloading first..."
    Invoke-DownloadWindows
  }

  # Check whether an instance is already running
  if (Test-Path -LiteralPath $PID_FILE) {
    $existingPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
    if ($existingPid) {
      $proc = Get-RunningProcess "$existingPid"
      if ($null -ne $proc) {
        Write-Host "Redis is already running (PID: $existingPid). Stopping first..."
        Invoke-StopWindows
      }
    }
  }

  New-Item -Path $WorkDir -ItemType Directory -Force | Out-Null

  Write-Host "Starting Redis on port $ClientPort..."

  # Build the argument list dynamically; --dir keeps rdb/aof files inside WorkDir
  $redisArgs = @()
  if (Test-Path -LiteralPath $REDIS_CONF) {
    $redisArgs += $REDIS_CONF
  }
  $redisArgs += @("--port", "$ClientPort", "--dir", $WorkDir, "--logfile", $LOG_FILE)

  $proc = Start-Process -FilePath $REDIS_SERVER_EXE -ArgumentList $redisArgs -PassThru -NoNewWindow `
    -RedirectStandardOutput (Join-Path $WorkDir "redis-stdout.log") `
    -RedirectStandardError (Join-Path $WorkDir "redis-stderr.log")

  Set-Content -Path $PID_FILE -Value $proc.Id
  Write-Host "Redis started with PID: $($proc.Id)"

  # Health check
  $maxRetries = 15
  $retryCount = 0
  $healthy = $false

  Write-Host "Waiting for Redis to respond to PING..."
  while ($retryCount -lt $maxRetries) {
    Start-Sleep -Seconds 1
    $retryCount++

    try {
      # Send PING via redis-cli; a PONG reply means the server is ready
      $result = & $REDIS_CLI_EXE -p $ClientPort PING 2>&1
      if ("$result" -eq "PONG") {
        $healthy = $true
        break
      }
    }
    catch {
      # Ignore connection errors while the server is still starting
    }

    # Check whether the process crashed during startup
    $checkProc = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if ($null -eq $checkProc -or $checkProc.HasExited) {
      Write-Error "Redis process died during startup. Check $LOG_FILE for details."
      exit 1
    }
  }

  if ($healthy) {
    Write-Host "Redis is healthy and ready on 127.0.0.1:${ClientPort}"
  }
  else {
    Write-Error "Redis failed to respond to PING within ${maxRetries}s. Check $LOG_FILE for details."
    Invoke-StopWindows
    exit 1
  }
}

function Invoke-StartUnix {
  $engine = Get-ContainerEngine
  Assert-ContainerEngineReady $engine

  & $engine image inspect $REDIS_IMAGE 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Host "Redis image not found. Pulling first..."
    Write-Host "Pulling $REDIS_IMAGE ..."
    & $engine pull $REDIS_IMAGE
    if ($LASTEXITCODE -ne 0) {
      Write-Error "Failed to pull $REDIS_IMAGE"
      exit 1
    }
  }

  if (Test-ContainerExists $engine) {
    if (Test-ContainerRunning $engine) {
      Write-Host "Redis container $CONTAINER_NAME is already running. Stopping first..."
      Invoke-StopUnix
    }
    else {
      & $engine rm $CONTAINER_NAME 2>$null | Out-Null
    }
  }

  New-Item -Path $WorkDir -ItemType Directory -Force | Out-Null

  Write-Host "Starting Redis container $CONTAINER_NAME on port $ClientPort..."

  $cid = & $engine run -d --name $CONTAINER_NAME -p "127.0.0.1:${ClientPort}:6379" $REDIS_IMAGE
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to start Redis container."
    exit 1
  }
  Set-Content -Path $CID_FILE -Value "$cid"
  Write-Host "Redis container started (ID: $cid)"

  # Health check; redis-cli is available inside the official image
  $maxRetries = 15
  $retryCount = 0
  $healthy = $false

  Write-Host "Waiting for Redis to respond to PING..."
  while ($retryCount -lt $maxRetries) {
    Start-Sleep -Seconds 1
    $retryCount++

    $result = & $engine exec $CONTAINER_NAME redis-cli PING 2>$null
    if ("$result" -eq "PONG") {
      $healthy = $true
      break
    }

    if (-not (Test-ContainerRunning $engine)) {
      Write-Error "Redis container died during startup. Check '$engine logs $CONTAINER_NAME' for details."
      exit 1
    }
  }

  if ($healthy) {
    Write-Host "Redis is healthy and ready on 127.0.0.1:${ClientPort}"
  }
  else {
    Write-Error "Redis failed to respond to PING within ${maxRetries}s. Check '$engine logs $CONTAINER_NAME' for details."
    Invoke-StopUnix
    exit 1
  }
}

function Invoke-Start {
  if ($IsWindowsHost) {
    Invoke-StartWindows
  }
  else {
    Invoke-StartUnix
  }
}

function Invoke-StopWindows {
  if (!(Test-Path -LiteralPath $PID_FILE)) {
    Write-Host "No PID file found. Redis may not be running."
    return
  }

  $redisPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($redisPid)) {
    Write-Host "PID file is empty."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  $proc = Get-RunningProcess $redisPid
  if ($null -eq $proc) {
    Write-Host "Redis process (PID: $redisPid) is not running."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  Write-Host "Stopping Redis (PID: $redisPid)..."

  # Try a graceful shutdown through redis-cli first
  if (Test-Path -LiteralPath $REDIS_CLI_EXE) {
    try {
      & $REDIS_CLI_EXE -p $ClientPort shutdown 2>&1 | Out-Null
    }
    catch {
      # The server may already be gone; fall through to the wait/kill logic.
    }
  }

  # Wait for the process to exit (up to 5 seconds)
  $waited = 0
  while ($waited -lt 5) {
    Start-Sleep -Seconds 1
    $waited++
    $checkProc = Get-Process -Id $redisPid -ErrorAction SilentlyContinue
    if ($null -eq $checkProc -or $checkProc.HasExited) {
      break
    }
  }

  # Force kill if the process is still alive
  $checkProc = Get-Process -Id $redisPid -ErrorAction SilentlyContinue
  if ($null -ne $checkProc -and !$checkProc.HasExited) {
    Write-Host "Force killing Redis (PID: $redisPid)..."
    Stop-Process -Id $redisPid -Force -ErrorAction SilentlyContinue
  }

  Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
  Write-Host "Redis stopped."
}

function Invoke-StopUnix {
  $engine = Find-ContainerEngine
  if ($null -eq $engine) {
    if (Test-Path -LiteralPath $CID_FILE) {
      Write-Error "No container engine available to stop $CONTAINER_NAME (state file: $CID_FILE)."
      exit 1
    }
    Write-Host "No container state found. Redis may not be running."
    return
  }

  if (-not (Test-ContainerExists $engine)) {
    Write-Host "Redis container $CONTAINER_NAME does not exist."
    Remove-Item -Force $CID_FILE -ErrorAction SilentlyContinue
    return
  }

  if (Test-ContainerRunning $engine) {
    Write-Host "Stopping Redis container $CONTAINER_NAME..."
    & $engine stop $CONTAINER_NAME 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Write-Error "Failed to stop Redis container $CONTAINER_NAME."
      exit 1
    }
  }

  & $engine rm $CONTAINER_NAME 2>$null | Out-Null
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to remove Redis container $CONTAINER_NAME."
    exit 1
  }

  Remove-Item -Force $CID_FILE -ErrorAction SilentlyContinue
  Write-Host "Redis stopped."
}

function Invoke-Stop {
  if ($IsWindowsHost) {
    Invoke-StopWindows
  }
  else {
    Invoke-StopUnix
  }
}

function Invoke-CleanupWindows {
  Invoke-StopWindows

  Write-Host "Cleaning up $WorkDir..."
  $filesToRemove = @($REDIS_SERVER_EXE, $REDIS_CLI_EXE, $REDIS_CONF, $LOG_FILE,
    (Join-Path $WorkDir "redis-stderr.log"), (Join-Path $WorkDir "redis-stdout.log"))

  foreach ($file in $filesToRemove) {
    if (Test-Path -LiteralPath $file) {
      Remove-Item -Force $file
    }
  }

  # Cleanup local persistence files (rdb/aof)
  Get-ChildItem -Path $WorkDir -Filter "*.rdb" -File -ErrorAction SilentlyContinue | Remove-Item -Force
  Get-ChildItem -Path $WorkDir -Filter "*.aof" -File -ErrorAction SilentlyContinue | Remove-Item -Force

  Write-Host "Cleanup complete."
}

function Invoke-CleanupUnix {
  Invoke-StopUnix

  Write-Host "Cleaning up $WorkDir..."
  if (Test-Path -LiteralPath $WorkDir) {
    Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
  }

  $engine = Find-ContainerEngine
  if ($null -ne $engine) {
    & $engine image inspect $REDIS_IMAGE 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
      & $engine rmi $REDIS_IMAGE 2>$null | Out-Null
      if ($LASTEXITCODE -ne 0) {
        Write-Host "Warning: failed to remove image $REDIS_IMAGE (it may be used elsewhere)."
      }
    }
  }

  Write-Host "Cleanup complete."
}

function Invoke-Cleanup {
  if ($IsWindowsHost) {
    Invoke-CleanupWindows
  }
  else {
    Invoke-CleanupUnix
  }
}

function Invoke-StatusWindows {
  if (!(Test-Path -LiteralPath $PID_FILE)) {
    Write-Host "Redis is not running (no PID file)."
    return
  }

  $redisPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($redisPid)) {
    Write-Host "Redis PID file is empty."
    return
  }

  $proc = Get-RunningProcess $redisPid
  if ($null -eq $proc) {
    Write-Host "Redis is not running (PID: $redisPid not found)."
    return
  }

  Write-Host "Redis is running (PID: $redisPid)."

  if (Test-Path -LiteralPath $REDIS_CLI_EXE) {
    try {
      $result = & $REDIS_CLI_EXE -p $ClientPort PING 2>&1
      Write-Host "Ping Response: $result"
    }
    catch {
      Write-Host "Ping check failed: $_"
    }
  }
}

function Invoke-StatusUnix {
  if (!(Test-Path -LiteralPath $CID_FILE)) {
    Write-Host "Redis is not running (no container state file)."
    return
  }

  $engine = Find-ContainerEngine
  if ($null -eq $engine) {
    Write-Host "Container state file exists ($CID_FILE) but no container engine is available."
    return
  }

  if (-not (Test-ContainerExists $engine)) {
    Write-Host "Redis is not running (container $CONTAINER_NAME not found)."
    return
  }

  if (-not (Test-ContainerRunning $engine)) {
    Write-Host "Redis container $CONTAINER_NAME exists but is not running."
    return
  }

  Write-Host "Redis is running (container $CONTAINER_NAME)."
  $result = & $engine exec $CONTAINER_NAME redis-cli PING 2>$null
  if ("$result" -eq "PONG") {
    Write-Host "Ping Response: $result"
  }
  else {
    Write-Host "Ping check failed."
  }
}

function Invoke-Status {
  if ($IsWindowsHost) {
    Invoke-StatusWindows
  }
  else {
    Invoke-StatusUnix
  }
}

switch ($Command) {
  "download" { Invoke-Download }
  "start" { Invoke-Start }
  "stop" { Invoke-Stop }
  "cleanup" { Invoke-Cleanup }
  "status" { Invoke-Status }
}
