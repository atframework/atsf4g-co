param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("download", "start", "stop", "cleanup", "status")]
  [string]$Command,

  [string]$WorkDir = "",

  [int]$ClientPort = 6379,

  [string]$RedisVersion = "latest"
)

$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'
$OutputEncoding = [System.Text.UTF8Encoding]::new()
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrEmpty($WorkDir)) {
  $WorkDir = Join-Path $env:TEMP "redis-unit-test"
}

$REDIS_SERVER_EXE = Join-Path $WorkDir "redis-server.exe"
$REDIS_CLI_EXE    = Join-Path $WorkDir "redis-cli.exe"
$REDIS_CONF       = Join-Path $WorkDir "redis.windows.conf"
$PID_FILE         = Join-Path $WorkDir "redis.pid"
$LOG_FILE         = Join-Path $WorkDir "redis.log"

function Invoke-Download {
  if ((Test-Path $REDIS_SERVER_EXE) -and (Test-Path $REDIS_CLI_EXE)) {
    Write-Host "Redis binaries already exist at $WorkDir, skipping download."
    Write-Host "Use 'cleanup' command first if you want to re-download."
    return
  }

  New-Item -Path $WorkDir -ItemType Directory -Force | Out-Null

  # 清理带有 'v' 的标签字符以匹配 zip 包命名 (例如 v5.0.14.1 -> 5.0.14.1)
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
  if (Test-Path $extractDir) {
    Remove-Item -Recurse -Force $extractDir
  }
  Expand-Archive -Path $zipFile -DestinationPath $extractDir -Force

  # 复制核心执行文件及默认配置文件
  Copy-Item -Path (Join-Path $extractDir "redis-server.exe") -Destination $REDIS_SERVER_EXE -Force
  Copy-Item -Path (Join-Path $extractDir "redis-cli.exe") -Destination $REDIS_CLI_EXE -Force
  if (Test-Path (Join-Path $extractDir "redis.windows.conf")) {
    Copy-Item -Path (Join-Path $extractDir "redis.windows.conf") -Destination $REDIS_CONF -Force
  }

  # 清理临时文件
  Remove-Item -Recurse -Force $extractDir
  Remove-Item -Force $zipFile

  Write-Host "Redis downloaded successfully to $WorkDir"
}

function Invoke-Start {
  if (!(Test-Path $REDIS_SERVER_EXE)) {
    Write-Host "Redis binary not found. Downloading first..."
    Invoke-Download
  }

  # 检查是否已有实例运行
  if (Test-Path $PID_FILE) {
    $existingPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
    if ($existingPid) {
      $proc = Get-Process -Id $existingPid -ErrorAction SilentlyContinue
      if ($null -ne $proc -and !$proc.HasExited) {
        Write-Host "Redis is already running (PID: $existingPid). Stopping first..."
        Invoke-Stop
      }
    }
  }

  Write-Host "Starting Redis on port $ClientPort..."

  # 动态组装启动参数
  $redisArgs = @()
  if (Test-Path $REDIS_CONF) {
    $redisArgs += $REDIS_CONF
  }
  $redisArgs += @("--port", $ClientPort, "--logfile", $LOG_FILE)

  # 后台静默启动进程
  $proc = Start-Process -FilePath $REDIS_SERVER_EXE -ArgumentList $redisArgs -PassThru -NoNewWindow -RedirectStandardError (Join-Path $WorkDir "redis-stderr.log")

  Set-Content -Path $PID_FILE -Value $proc.Id
  Write-Host "Redis started with PID: $($proc.Id)"

  # 连通性健康检查
  $maxRetries = 15
  $retryCount = 0
  $healthy = $false

  Write-Host "Waiting for Redis to respond to PING..."
  while ($retryCount -lt $maxRetries) {
    Start-Sleep -Seconds 1
    $retryCount++

    try {
      # 使用 redis-cli 发送 PING，若返回 PONG 则代表正常响应
      $result = & $REDIS_CLI_EXE -p $ClientPort PING 2>&1
      if ($result -eq "PONG") {
        $healthy = $true
        break
      }
    }
    catch {
      # 忽略启动中的连接报错
    }

    # 检查进程中途是否崩溃
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
    Invoke-Stop
    exit 1
  }
}

function Invoke-Stop {
  if (!(Test-Path $PID_FILE)) {
    Write-Host "No PID file found. Redis may not be running."
    return
  }

  $redisPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($redisPid)) {
    Write-Host "PID file is empty."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  $proc = Get-Process -Id $redisPid -ErrorAction SilentlyContinue
  if ($null -eq $proc -or $proc.HasExited) {
    Write-Host "Redis process (PID: $redisPid) is not running."
    Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
    return
  }

  Write-Host "Stopping Redis (PID: $redisPid)..."
  
  # 优先尝试优雅关闭客户端
  if (Test-Path $REDIS_CLI_EXE) {
    & $REDIS_CLI_EXE -p $ClientPort shutdown 2>&1 | Out-Null
  }

  # 等待进程退出 (最多 5 秒)
  $waited = 0
  while ($waited -lt 5) {
    Start-Sleep -Seconds 1
    $waited++
    $checkProc = Get-Process -Id $redisPid -ErrorAction SilentlyContinue
    if ($null -eq $checkProc -or $checkProc.HasExited) {
      break
    }
  }

  # 如果进程依然存活，强制杀进程
  $checkProc = Get-Process -Id $redisPid -ErrorAction SilentlyContinue
  if ($null -ne $checkProc -and !$checkProc.HasExited) {
    Write-Host "Force killing Redis (PID: $redisPid)..."
    Stop-Process -Id $redisPid -Force -ErrorAction SilentlyContinue
  }

  Remove-Item -Force $PID_FILE -ErrorAction SilentlyContinue
  Write-Host "Redis stopped."
}

function Invoke-Cleanup {
  Invoke-Stop

  Write-Host "Cleaning up $WorkDir..."
  $filesToRemove = @($REDIS_SERVER_EXE, $REDIS_CLI_EXE, $REDIS_CONF, $LOG_FILE, (Join-Path $WorkDir "redis-stderr.log"))

  foreach ($file in $filesToRemove) {
    if (Test-Path $file) { Remove-Item -Force $file }
  }

  # 清理本地默认持久化文件 (rdb/aof)
  Get-ChildItem -Path $WorkDir -Include "*.rdb","*.aof" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force

  Write-Host "Cleanup complete."
}

function Invoke-Status {
  if (!(Test-Path $PID_FILE)) {
    Write-Host "Redis is not running (no PID file)."
    return
  }

  $redisPid = Get-Content $PID_FILE -ErrorAction SilentlyContinue
  if ([string]::IsNullOrEmpty($redisPid)) {
    Write-Host "Redis PID file is empty."
    return
  }

  $proc = Get-Process -Id $redisPid -ErrorAction SilentlyContinue
  if ($null -eq $proc -or $proc.HasExited) {
    Write-Host "Redis is not running (PID: $redisPid not found)."
    return
  }

  Write-Host "Redis is running (PID: $redisPid)."

  if (Test-Path $REDIS_CLI_EXE) {
    try {
      $result = & $REDIS_CLI_EXE -p $ClientPort PING 2>&1
      Write-Host "Ping Response: $result"
    }
    catch {
      Write-Host "Ping check failed: $_"
    }
  }
}

switch ($Command) {
  "download" { Invoke-Download }
  "start"    { Invoke-Start }
  "stop"     { Invoke-Stop }
  "cleanup"  { Invoke-Cleanup }
  "status"   { Invoke-Status }
}