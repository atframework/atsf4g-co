{{- define "atapp.lifecycle.ps1" -}}
{{- $context := .context -}}
{{- $bus_addr := include "libapp.busAddr" $context -}}
#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$Executable = Join-Path $PSScriptRoot '{{ $context.Values.proc_name | replace "'" "''" }}.exe'
$PidFile = '{{ $context.Values.type_name | replace "'" "''" }}_{{ $bus_addr }}.pid'
$ProcessRecord = "$PidFile.process"
$StartupErrorFile = [IO.Path]::ChangeExtension($PidFile, 'startup-error')
$ConfigFile = '../cfg/{{ include "libapp.name" $context | replace "'" "''" }}_{{ $bus_addr }}.yaml'
$CommandArguments = @('-id', '{{ $bus_addr }}', '-p', $PidFile, '--startup-error-file', $StartupErrorFile, '-c', $ConfigFile)
$Action = '{{ .action }}'

function Get-ServicePid {
    [int]$ServicePid = 0
    if (Test-Path -LiteralPath $PidFile -PathType Leaf) {
        [void][int]::TryParse((Get-Content -LiteralPath $PidFile -Raw), [ref]$ServicePid)
    }
    return $ServicePid
}

function Get-ServiceProcess {
    $RecordedStart = $null
    if (Test-Path -LiteralPath $ProcessRecord -PathType Leaf) {
        $Identity = [string](Get-Content -LiteralPath $ProcessRecord -Raw)
        if ($Identity -notmatch '^\s*(\d+) (\d+)\s*$') {
            throw "Invalid process record: $ProcessRecord"
        }
        $ServicePid = [int]$Matches[1]
        $RecordedStart = [long]$Matches[2]
    } else {
        # Older deployments only have the atapp PID file.
        $ServicePid = Get-ServicePid
    }

    if ($ServicePid -le 0) {
        return
    }

    $Process = Get-Process -Id $ServicePid -ErrorAction SilentlyContinue
    if ($Process -and $Process.Path -eq $Executable) {
        if ($null -ne $RecordedStart -and $Process.StartTime.ToUniversalTime().Ticks -ne $RecordedStart) {
            return
        }
        return $Process
    }
}

function Start-ServiceInstance {
    param ([string[]]$ExtraArguments)

    if (Get-ServiceProcess) {
        Write-Host "[INFO] $PidFile is already running."
        return 0
    }

    foreach ($File in $PidFile, $StartupErrorFile) {
        if (Test-Path -LiteralPath $File) {
            Remove-Item -LiteralPath $File
        }
    }
    $LogDir = Join-Path $PSScriptRoot '../log'
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    $LogPrefix = Join-Path $LogDir ([IO.Path]::GetFileNameWithoutExtension($PidFile))
    $StartArguments = @('-env', '{{ $context.Values.atapp.deployment.deployment_environment | replace "'" "''" }}') +
        $CommandArguments + 'start' + $ExtraArguments

    # Start-Process joins ArgumentList with spaces. Escape each argument for the Windows CRT.
    $QuotedArguments = foreach ($Argument in $StartArguments) {
        '"' + ($Argument -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
    }
    $ProcessOptions = @{
        FilePath = $Executable
        ArgumentList = $QuotedArguments
        WorkingDirectory = $PSScriptRoot
        WindowStyle = 'Hidden'
        RedirectStandardOutput = "$LogPrefix.stdout.log"
        RedirectStandardError = "$LogPrefix.stderr.log"
        PassThru = $true
    }
    $Process = Start-Process @ProcessOptions

    $Timer = [Diagnostics.Stopwatch]::StartNew()
    while ($Timer.Elapsed.TotalSeconds -lt 20 -and -not $Process.HasExited) {
        if ((Get-ServicePid) -eq $Process.Id) {
            try {
                $Identity = "$($Process.Id) $($Process.StartTime.ToUniversalTime().Ticks)"
                Set-Content -LiteralPath "$ProcessRecord.tmp" -Value $Identity
                [IO.File]::Move((Join-Path $PSScriptRoot "$ProcessRecord.tmp"), (Join-Path $PSScriptRoot $ProcessRecord), $true)
            } catch {
                Stop-Process -InputObject $Process -ErrorAction SilentlyContinue
                throw
            }
            Write-Host "[INFO] $PidFile started (PID $($Process.Id))."
            return 0
        }
        if (Test-Path -LiteralPath $StartupErrorFile -PathType Leaf) {
            break
        }
        Start-Sleep -Milliseconds 100
    }

    if (-not $Process.HasExited) {
        Stop-Process -InputObject $Process -ErrorAction SilentlyContinue
        if (-not $Process.WaitForExit(5000)) {
            throw "Startup failed and PID $($Process.Id) is still running. See $LogPrefix.stderr.log."
        }
    }
    Write-Host "[ERROR] Startup failed for $PidFile (exit $($Process.ExitCode)). See $LogPrefix.stderr.log and $StartupErrorFile."
    if ($Process.ExitCode -ne 0) {
        return $Process.ExitCode
    }
    return 1
}

function Stop-ServiceInstance {
    param ([string[]]$ExtraArguments)

    $Process = Get-ServiceProcess
    if (-not $Process) {
        Write-Host "[INFO] $PidFile is stopped."
        return 0
    }

    & $Executable @CommandArguments stop @ExtraArguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        return $LASTEXITCODE
    }
    if (-not $Process.WaitForExit(30000)) {
        Write-Host "[ERROR] $PidFile is still running after the stop command."
        return 1
    }
    return 0
}

$OriginalPath = $env:PATH
$InstallRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$env:PATH = (Join-Path $InstallRoot 'bin'), (Join-Path $InstallRoot 'lib'), $OriginalPath -join [IO.Path]::PathSeparator
Push-Location -LiteralPath $PSScriptRoot
try {
    # Serialize commands for this PID file across independent PowerShell processes.
    $InstancePath = (Join-Path $PSScriptRoot $PidFile).ToUpperInvariant()
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $InstanceHash = [BitConverter]::ToString($Hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($InstancePath)))
    } finally {
        $Hasher.Dispose()
    }
    $Mutex = [Threading.Mutex]::new($false, "atsf4g-$InstanceHash")
    $Locked = $false
    try {
        try {
            $Locked = $Mutex.WaitOne([TimeSpan]::FromSeconds(60))
        } catch [Threading.AbandonedMutexException] {
            $Locked = $true
        }
        if (-not $Locked) {
            throw "Timed out waiting for another operation on $PidFile."
        }

        if ($Action -in 'start', 'restart' -or (Get-ServiceProcess)) {
            if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
                throw "Service executable not found: $Executable"
            }
            if (-not (Test-Path -LiteralPath $ConfigFile -PathType Leaf)) {
                throw "Service configuration not found: $ConfigFile"
            }
        } else {
            Write-Host "[INFO] $PidFile is stopped."
            exit 0
        }

        switch ($Action) {
            'start' { exit (Start-ServiceInstance -ExtraArguments $args) }
            'stop' { exit (Stop-ServiceInstance -ExtraArguments $args) }
            'restart' {
                $Result = Stop-ServiceInstance -ExtraArguments @('--upgrade')
                if ($Result -ne 0) {
                    exit $Result
                }
                exit (Start-ServiceInstance -ExtraArguments $args)
            }
            default {
                & $Executable @CommandArguments $Action @args
                exit $LASTEXITCODE
            }
        }
    } finally {
        if ($Locked) {
            $Mutex.ReleaseMutex()
        }
        $Mutex.Dispose()
    }
} finally {
    Pop-Location
    $env:PATH = $OriginalPath
}
{{- end }}

{{- define "atapp.start.ps1" -}}
{{ include "atapp.lifecycle.ps1" (dict "context" . "action" "start") }}
{{- end }}

{{- define "atapp.stop.ps1" -}}
{{ include "atapp.lifecycle.ps1" (dict "context" . "action" "stop") }}
{{- end }}

{{- define "atapp.restart.ps1" -}}
{{ include "atapp.lifecycle.ps1" (dict "context" . "action" "restart") }}
{{- end }}

{{- define "atapp.reload.ps1" -}}
{{ include "atapp.lifecycle.ps1" (dict "context" . "action" "reload") }}
{{- end }}

{{- define "atapp.runcmd.ps1" -}}
{{ include "atapp.lifecycle.ps1" (dict "context" . "action" "run") }}
{{- end }}
