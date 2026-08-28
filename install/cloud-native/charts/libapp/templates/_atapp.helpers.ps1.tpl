{{- define "atapp.call_sh.ps1" -}}
$ErrorActionPreference = 'Stop'

$env:PATH = (Join-Path $PSScriptRoot '..\..\lib') + ';' + $env:PATH
$env:PATH = (Join-Path $PSScriptRoot '..\..\bin') + ';' + $env:PATH

$TargetSh = Join-Path $PSScriptRoot '{{ .script_name }}'

if (-not (Test-Path -LiteralPath $TargetSh)) {
	Write-Host ('[ERROR] Missing shell script: "{0}"' -f $TargetSh)
	exit 1
}

$GitShExecutable = ''

function Find-GitShExecutable {
	$Candidates = @()
	if ($env:ProgramFiles) {
		$Candidates += (Join-Path $env:ProgramFiles 'Git\bin\sh.exe')
		$Candidates += (Join-Path $env:ProgramFiles 'Git\usr\bin\sh.exe')
	}
	if (${env:ProgramFiles(x86)}) {
		$Candidates += (Join-Path ${env:ProgramFiles(x86)} 'Git\bin\sh.exe')
		$Candidates += (Join-Path ${env:ProgramFiles(x86)} 'Git\usr\bin\sh.exe')
	}
	if ($env:LocalAppData) {
		$Candidates += (Join-Path $env:LocalAppData 'Programs\Git\bin\sh.exe')
		$Candidates += (Join-Path $env:LocalAppData 'Programs\Git\usr\bin\sh.exe')
	}

	foreach ($Candidate in $Candidates) {
		if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
			return $Candidate
		}
	}

	foreach ($GitExecutable in @(Get-Command 'git.exe' -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })) {
		$GitRoot = Split-Path -Parent (Split-Path -Parent $GitExecutable)
		foreach ($RelativePath in @('bin\sh.exe', 'usr\bin\sh.exe')) {
			$Candidate = Join-Path $GitRoot $RelativePath
			if (-not $GitShExecutable -and (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
				return $Candidate
			}
		}
	}

	foreach ($ShExecutable in @(Get-Command 'sh.exe' -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })) {
		return $ShExecutable
	}

	return ''
}

$GitShExecutable = Find-GitShExecutable

if ([string]::IsNullOrEmpty($GitShExecutable)) {
	Write-Host '[ERROR] Failed to locate Git sh.exe. Please install Git for Windows or add Git to PATH.'
	exit 1
}

& $GitShExecutable ($TargetSh -replace '\\', '/') @args
exit $LASTEXITCODE
{{- end }}

{{- define "atapp.reload.ps1" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.ps1" (dict "script_name" (printf "reload_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.restart.ps1" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.ps1" (dict "script_name" (printf "restart_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.runcmd.ps1" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.ps1" (dict "script_name" (printf "runcmd_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.start.ps1" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.ps1" (dict "script_name" (printf "start_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.stop.ps1" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.ps1" (dict "script_name" (printf "stop_%s.sh" $bus_addr)) }}
{{- end }}
