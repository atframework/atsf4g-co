$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

# This script must work on: Windows PowerShell 5.1, pwsh 7 on Windows, and pwsh 7 on
# Linux/macOS. Rules applied below:
#   * All paths use forward slashes ('/'): PowerShell accepts them on every platform while
#     backslashes ('\') break non-Windows hosts.
#   * Child scripts run in their own pwsh process via `pwsh -NoProfile -File`, never by
#     relying on `pwsh <script.ps1>` name resolution.
#   * The pwsh executable is resolved explicitly instead of trusting PATH.

function Get-PwshExecutable {
    if ($PSVersionTable.PSEdition -eq 'Core') {
        # Already running inside pwsh (PowerShell 7+). $PSHOME always contains the binary of
        # the running engine, so no PATH lookup is required.
        if ($IsWindows) {
            return (Join-Path $PSHOME 'pwsh.exe')
        }
        return (Join-Path $PSHOME 'pwsh')
    }

    # Windows PowerShell 5.1 host: locate a pwsh (PowerShell 7+) installation.
    $command = Get-Command -Name pwsh -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    # Microsoft Store (MSIX) installs only expose pwsh.exe through a per-user App Execution
    # Alias under %LOCALAPPDATA%\Microsoft\WindowsApps (part of the *user* PATH). Processes
    # that inherit a stale or system-only PATH - cmd.exe started by services, CI runners,
    # IDE tasks, or shells elevated under another profile - cannot resolve `pwsh` even
    # though interactive Windows Terminal / PowerShell sessions can. Probe the well known
    # locations directly to cover those hosts.
    $candidates = @()
    if ($env:ProgramFiles) {
        $candidates += (Join-Path $env:ProgramFiles 'PowerShell/7/pwsh.exe')
        $candidates += (Join-Path $env:ProgramFiles 'PowerShell/7-preview/pwsh.exe')
    }
    if (${env:ProgramFiles(x86)}) {
        $candidates += (Join-Path ${env:ProgramFiles(x86)} 'PowerShell/7/pwsh.exe')
    }
    if ($env:LOCALAPPDATA) {
        $candidates += (Join-Path $env:LOCALAPPDATA 'Microsoft/WindowsApps/pwsh.exe')
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    Write-Error ('PowerShell 7+ (pwsh) is required to run the dependency scripts. ' +
        'Install it with `winget install --source winget Microsoft.PowerShell` (MSI, system PATH) ' +
        'or from GitHub releases. Microsoft Store installs only add a per-user PATH alias and are ' +
        'not visible to every subprocess environment.')
    exit 1
}

function Invoke-PwshScript {
    param(
        [Parameter(Mandatory = $true)][string]$ScriptPath,
        [Parameter(Mandatory = $true)][string]$Command
    )

    # -File guarantees deterministic behavior across subprocess environments (cmd.exe, batch
    # files, CI runners, cross-shell invocations): script arguments are passed as literal
    # strings and the script's `exit <code>` becomes the process exit code. Without it,
    # Windows PowerShell 5.1 defaults to -Command, which re-parses quoted arguments and
    # collapses every nonzero exit code to 1.
    # -ExecutionPolicy Bypass keeps the child runnable under a Restricted machine policy;
    # it is ignored on non-Windows platforms.
    & $script:PwshExecutable -NoProfile -ExecutionPolicy Bypass -File $ScriptPath $Command
    if ($LASTEXITCODE -ne 0) {
        Write-Host ('[ERROR] {0} failed with exit code {1}' -f $ScriptPath, $LASTEXITCODE)
        exit $LASTEXITCODE
    }
}

$script:PwshExecutable = Get-PwshExecutable

Invoke-PwshScript -ScriptPath (Join-Path $PSScriptRoot 'etcd/setup-etcd.ps1') -Command 'start'
Invoke-PwshScript -ScriptPath (Join-Path $PSScriptRoot 'redis/redis.ps1') -Command 'start'

# $env:OS is 'Windows_NT' on every Windows host (5.1 and 7+); the $IsWindows automatic
# variable only exists on PowerShell 7.
$IsWindowsHost = ($env:OS -eq 'Windows_NT')

$OtelBinDir = Join-Path $PSScriptRoot '../../otelcol/bin'
$OtelExecutableName = 'otelcol-contrib'
if ($IsWindowsHost) {
    $OtelExecutableName = 'otelcol-contrib.exe'
}
$OtelExecutable = Join-Path $OtelBinDir $OtelExecutableName
if (-not (Test-Path -LiteralPath $OtelExecutable -PathType Leaf)) {
    Write-Host ('[ERROR] Otel collector executable does not exist: "{0}"' -f $OtelExecutableName)
    exit 1
}

$LogDir = Join-Path $OtelBinDir '../log'
if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

Set-Location -LiteralPath $OtelBinDir
& $OtelExecutable '--config=../cfg/config.yaml'
exit $LASTEXITCODE
