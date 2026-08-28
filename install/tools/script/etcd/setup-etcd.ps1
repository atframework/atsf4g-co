# Copyright 2026 atframework
# PowerShell script to download, start, stop, and manage etcd for unit testing.
# Works on Windows PowerShell 5.1 and pwsh 7+ on Windows/Linux/macOS.
# Usage: setup-etcd.ps1 -Command <download|start|stop|cleanup|status>
#   -WorkDir DIR          Working directory (default: <temp dir>/etcd-unit-test)
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
  # [System.IO.Path]::GetTempPath() resolves $env:TEMP on Windows and TMPDIR (or /tmp) on
  # Linux/macOS; $env:TEMP does not exist on Unix hosts.
  $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) "etcd-unit-test"
}

$ExeSuffix = ""
if ($IsWindowsHost) {
  $ExeSuffix = ".exe"
}

$ETCD_EXE = Join-Path $WorkDir "etcd$ExeSuffix"
$ETCDCTL_EXE = Join-Path $WorkDir "etcdctl$ExeSuffix"
$PID_FILE = Join-Path $WorkDir "etcd.pid"
$LOG_FILE = Join-Path $WorkDir "etcd.log"
$DATA_DIR = Join-Path $WorkDir "data"

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

  if ((Test-Path -LiteralPath $ETCD_EXE) -and (Test-Path -LiteralPath $ETCDCTL_EXE)) {
    Write-Host "etcd binaries already exist at $WorkDir, skipping download."
    Write-Host "Use 'cleanup' command first if you want to re-download."
    return
  }

  New-Item -Path $WorkDir -ItemType Directory -Force | Out-Null

  # $env:PROCESSOR_ARCHITECTURE is Windows only; on Unix hosts use
  # System.Runtime.InteropServices.RuntimeInformation (with a fallback for Windows
  # PowerShell 5.1 running on old .NET Framework builds).
  $arch = "amd64"
  try {
    if ([System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -eq
        [System.Runtime.InteropServices.Architecture]::Arm64) {
      $arch = "arm64"
    }
  }
  catch {
    if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") {
      $arch = "arm64"
    }
  }

  $platform = "${HostOs}-${arch}"
  $archiveExt = ".zip"
  if ($HostOs -eq "linux") {
    $archiveExt = ".tar.gz"
  }

  $downloadUrl = "https://github.com/etcd-io/etcd/releases/download/${tag}/etcd-${tag}-${platform}${archiveExt}"
  $archiveFile = Join-Path $WorkDir "etcd$archiveExt"

  Write-Host "Downloading etcd ${tag} for ${platform}..."
  Write-Host "URL: $downloadUrl"

  try {
    Invoke-WebRequest -Uri $downloadUrl -OutFile $archiveFile -UseBasicParsing
  }
  catch {
    Write-Error "Failed to download etcd: $_"
    exit 1
  }

  Write-Host "Extracting..."
  $extractDir = Join-Path $WorkDir "etcd-extract"
  if (Test-Path -LiteralPath $extractDir) {
    Remove-Item -Recurse -Force $extractDir
  }
  if ($archiveExt -eq ".zip") {
    Expand-Archive -Path $archiveFile -DestinationPath $extractDir -Force
  }
  else {
    & tar -xzf $archiveFile -C $extractDir
    if ($LASTEXITCODE -ne 0) {
      Write-Error "Failed to extract etcd archive: $archiveFile"
      exit 1
    }
  }

  # Find the extracted directory (etcd-vX.Y.Z-<os>-<arch>/)
  $innerDir = Get-ChildItem -Path $extractDir -Directory | Select-Object -First 1
  if ($null -eq $innerDir) {
    Write-Error "Unexpected etcd archive layout: no inner directory found in $extractDir"
    exit 1
  }

  Copy-Item -Path (Join-Path $innerDir.FullName "etcd$ExeSuffix") -Destination $ETCD_EXE -Force
  Copy-Item -Path (Join-Path $innerDir.FullName "etcdctl$ExeSuffix") -Destination $ETCDCTL_EXE -Force

  if (-not $IsWindowsHost) {
    # Archive extraction may not preserve the executable permission on Unix.
    & chmod +x $ETCD_EXE $ETCDCTL_EXE
    if ($LASTEXITCODE -ne 0) {
      Write-Error "Failed to set executable permission on etcd binaries."
      exit 1
    }
  }

  # Cleanup temporary files
  Remove-Item -Recurse -Force $extractDir
  Remove-Item -Force $archiveFile

  Write-Host "etcd downloaded successfully to $WorkDir"
}

function Invoke-Start {
  if (!(Test-Path -LiteralPath $ETCD_EXE)) {
    Write-Host "etcd binary not found. Downloading first..."
    Invoke-Download
  }

  # Check if already running
  if (Test-Path -LiteralPath $PID_FILE) {
    $existingPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
    if ($existingPid) {
      $proc = Get-RunningProcess "$existingPid"
      if ($null -ne $proc) {
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

  # Redirect both stdout and stderr: Start-Process on Unix hosts requires the streams to be
  # consumed when the child outlives the caller.
  $proc = Start-Process -FilePath $ETCD_EXE -ArgumentList $etcdArgs -PassThru -NoNewWindow `
    -RedirectStandardOutput (Join-Path $WorkDir "etcd-stdout.log") `
    -RedirectStandardError (Join-Path $WorkDir "etcd-stderr.log")

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
  if (!(Test-Path -LiteralPath $PID_FILE)) {
    Write-Host "No PID file found. etcd may not be running."
    return
  }

  $etcdPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($etcdPid)) {
    Write-Host "PID file is empty."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  $proc = Get-RunningProcess $etcdPid
  if ($null -eq $proc) {
    Write-Host "etcd process (PID: $etcdPid) is not running."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  Write-Host "Stopping etcd (PID: $etcdPid)..."
  # Stop-Process -Force terminates via TerminateProcess on Windows and SIGKILL on Unix.
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
  if (Test-Path -LiteralPath $DATA_DIR) {
    Remove-Item -Recurse -Force $DATA_DIR
  }
  foreach ($file in @($ETCD_EXE, $ETCDCTL_EXE, $LOG_FILE,
      (Join-Path $WorkDir "etcd-stderr.log"), (Join-Path $WorkDir "etcd-stdout.log"))) {
    if (Test-Path -LiteralPath $file) {
      Remove-Item -Force $file
    }
  }

  Write-Host "Cleanup complete."
}

function Invoke-Status {
  if (!(Test-Path -LiteralPath $PID_FILE)) {
    Write-Host "etcd is not running (no PID file)."
    return
  }

  $etcdPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($etcdPid)) {
    Write-Host "etcd PID file is empty."
    return
  }

  $proc = Get-RunningProcess $etcdPid
  if ($null -eq $proc) {
    Write-Host "etcd is not running (PID: $etcdPid not found)."
    return
  }

  Write-Host "etcd is running (PID: $etcdPid)."

  if (Test-Path -LiteralPath $ETCDCTL_EXE) {
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
