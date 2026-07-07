# Copyright 2026 atframework
# PowerShell script to download, start, stop, and manage etcd for unit testing.
# Usage: setup-etcd.ps1 -Command <download|start|stop|cleanup|status>
#   -WorkDir DIR          Working directory (default: $env:TEMP\etcd-unit-test)
#   -ClientPort PORT      Client port (default: 12379)
#   -PeerPort PORT        Peer port (default: 12380)
#   -EtcdVersion VER      Specify version tag (default: latest)

param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("download", "start", "stop", "cleanup", "status")]
  [string]$Command,

  [string]$WorkDir = "",

  [int]$ClientPort = 12379,

  [int]$PeerPort = 12380,

  [string]$EtcdVersion = "latest"
)

$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'
$OutputEncoding = [System.Text.UTF8Encoding]::new()
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrEmpty($WorkDir)) {
  $WorkDir = Join-Path $env:TEMP "etcd-unit-test"
}

$ETCD_EXE = Join-Path $WorkDir "etcd.exe"
$ETCDCTL_EXE = Join-Path $WorkDir "etcdctl.exe"
$PID_FILE = Join-Path $WorkDir "etcd.pid"
$LOG_FILE = Join-Path $WorkDir "etcd.log"
$DATA_DIR = Join-Path $WorkDir "data"

function Get-EtcdVersion {
  if ($EtcdVersion -ne "latest") {
    return $EtcdVersion
  }

  Write-Host "Fetching latest etcd version from GitHub..."
  try {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/etcd-io/etcd/releases/latest" -UseBasicParsing
    $tag = $release.tag_name
    Write-Host "Latest etcd version: $tag"
    return $tag
  }
  catch {
    Write-Error "Failed to fetch latest etcd version: $_"
    exit 1
  }
}

function Invoke-Download {
  $tag = Get-EtcdVersion

  if ((Test-Path $ETCD_EXE) -and (Test-Path $ETCDCTL_EXE)) {
    Write-Host "etcd binaries already exist at $WorkDir, skipping download."
    Write-Host "Use 'cleanup' command first if you want to re-download."
    return
  }

  New-Item -Path $WorkDir -ItemType Directory -Force | Out-Null

  $arch = "amd64"
  if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") {
    $arch = "arm64"
  }

  $downloadUrl = "https://github.com/etcd-io/etcd/releases/download/${tag}/etcd-${tag}-windows-${arch}.zip"
  $zipFile = Join-Path $WorkDir "etcd.zip"

  Write-Host "Downloading etcd ${tag} for windows-${arch}..."
  Write-Host "URL: $downloadUrl"

  try {
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipFile -UseBasicParsing
  }
  catch {
    Write-Error "Failed to download etcd: $_"
    exit 1
  }

  Write-Host "Extracting..."
  $extractDir = Join-Path $WorkDir "etcd-extract"
  if (Test-Path $extractDir) {
    Remove-Item -Recurse -Force $extractDir
  }
  Expand-Archive -Path $zipFile -DestinationPath $extractDir -Force

  # Find the extracted directory (etcd-vX.Y.Z-windows-amd64/)
  $innerDir = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1

  Copy-Item -Path (Join-Path $innerDir.FullName "etcd.exe") -Destination $ETCD_EXE -Force
  Copy-Item -Path (Join-Path $innerDir.FullName "etcdctl.exe") -Destination $ETCDCTL_EXE -Force

  # Cleanup temporary files
  Remove-Item -Recurse -Force $extractDir
  Remove-Item -Force $zipFile

  Write-Host "etcd downloaded successfully to $WorkDir"
}

function Invoke-Start {
  if (!(Test-Path $ETCD_EXE)) {
    Write-Host "etcd binary not found. Downloading first..."
    Invoke-Download
  }

  # Check if already running
  if (Test-Path $PID_FILE) {
    $existingPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
    if ($existingPid) {
      $proc = Get-Process -Id $existingPid -ErrorAction SilentlyContinue
      if ($null -ne $proc -and !$proc.HasExited) {
        Write-Host "etcd is already running (PID: $existingPid). Stopping first..."
        Invoke-Stop
      }
    }
  }

  New-Item -Path $DATA_DIR -ItemType Directory -Force | Out-Null

  Write-Host "Starting etcd on client port $ClientPort, peer port $PeerPort..."

  $etcdArgs = @(
    "--data-dir", $DATA_DIR,
    "--listen-client-urls", "http://127.0.0.1:${ClientPort}",
    "--advertise-client-urls", "http://127.0.0.1:${ClientPort}",
    "--listen-peer-urls", "http://127.0.0.1:${PeerPort}",
    "--initial-advertise-peer-urls", "http://127.0.0.1:${PeerPort}",
    "--initial-cluster", "default=http://127.0.0.1:${PeerPort}",
    "--log-outputs", $LOG_FILE
  )

  $proc = Start-Process -FilePath $ETCD_EXE -ArgumentList $etcdArgs -PassThru -NoNewWindow -RedirectStandardError (Join-Path $WorkDir "etcd-stderr.log")

  Set-Content -Path $PID_FILE -Value $proc.Id

  Write-Host "etcd started with PID: $($proc.Id)"

  # Health check with retries
  $maxRetries = 30
  $retryCount = 0
  $healthy = $false

  Write-Host "Waiting for etcd to become healthy..."
  while ($retryCount -lt $maxRetries) {
    Start-Sleep -Seconds 1
    $retryCount++

    try {
      $result = & $ETCDCTL_EXE --endpoints="http://127.0.0.1:${ClientPort}" endpoint health 2>&1
      if ($LASTEXITCODE -eq 0) {
        $healthy = $true
        break
      }
    }
    catch {
      # Ignore errors during startup
    }

    # Check if process is still alive
    $checkProc = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if ($null -eq $checkProc -or $checkProc.HasExited) {
      Write-Error "etcd process died during startup. Check $LOG_FILE for details."
      exit 1
    }
  }

  if ($healthy) {
    Write-Host "etcd is healthy and ready on http://127.0.0.1:${ClientPort}"
  }
  else {
    Write-Error "etcd failed to become healthy within ${maxRetries}s. Check $LOG_FILE for details."
    Invoke-Stop
    exit 1
  }
}

function Invoke-Stop {
  if (!(Test-Path $PID_FILE)) {
    Write-Host "No PID file found. etcd may not be running."
    return
  }

  $etcdPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($etcdPid)) {
    Write-Host "PID file is empty."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  $proc = Get-Process -Id $etcdPid -ErrorAction SilentlyContinue
  if ($null -eq $proc -or $proc.HasExited) {
    Write-Host "etcd process (PID: $etcdPid) is not running."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  Write-Host "Stopping etcd (PID: $etcdPid)..."
  Stop-Process -Id $etcdPid -Force -ErrorAction SilentlyContinue

  # Wait for process to exit (up to 5 seconds)
  $waited = 0
  while ($waited -lt 5) {
    Start-Sleep -Seconds 1
    $waited++
    $checkProc = Get-Process -Id $etcdPid -ErrorAction SilentlyContinue
    if ($null -eq $checkProc -or $checkProc.HasExited) {
      break
    }
  }

  # Force kill if still alive
  $checkProc = Get-Process -Id $etcdPid -ErrorAction SilentlyContinue
  if ($null -ne $checkProc -and !$checkProc.HasExited) {
    Write-Host "Force killing etcd (PID: $etcdPid)..."
    Stop-Process -Id $etcdPid -Force -ErrorAction SilentlyContinue
  }

  Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
  Write-Host "etcd stopped."
}

function Invoke-Cleanup {
  Invoke-Stop

  Write-Host "Cleaning up $WorkDir..."
  if (Test-Path $DATA_DIR) {
    Remove-Item -Recurse -Force $DATA_DIR
  }
  if (Test-Path $ETCD_EXE) {
    Remove-Item -Force $ETCD_EXE
  }
  if (Test-Path $ETCDCTL_EXE) {
    Remove-Item -Force $ETCDCTL_EXE
  }
  if (Test-Path $LOG_FILE) {
    Remove-Item -Force $LOG_FILE
  }
  $stderrLog = Join-Path $WorkDir "etcd-stderr.log"
  if (Test-Path $stderrLog) {
    Remove-Item -Force $stderrLog
  }

  Write-Host "Cleanup complete."
}

function Invoke-Status {
  if (!(Test-Path $PID_FILE)) {
    Write-Host "etcd is not running (no PID file)."
    return
  }

  $etcdPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($etcdPid)) {
    Write-Host "etcd PID file is empty."
    return
  }

  $proc = Get-Process -Id $etcdPid -ErrorAction SilentlyContinue
  if ($null -eq $proc -or $proc.HasExited) {
    Write-Host "etcd is not running (PID: $etcdPid not found)."
    return
  }

  Write-Host "etcd is running (PID: $etcdPid)."

  if (Test-Path $ETCDCTL_EXE) {
    try {
      $result = & $ETCDCTL_EXE --endpoints="http://127.0.0.1:${ClientPort}" endpoint health 2>&1
      Write-Host "Health: $result"
    }
    catch {
      Write-Host "Health check failed: $_"
    }
  }
}

switch ($Command) {
  "download" { Invoke-Download }
  "start" { Invoke-Start }
  "stop" { Invoke-Stop }
  "cleanup" { Invoke-Cleanup }
  "status" { Invoke-Status }
}
