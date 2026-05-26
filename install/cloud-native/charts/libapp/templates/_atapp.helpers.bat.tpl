{{- define "atapp.call_sh.bat" -}}
@echo off
setlocal EnableExtensions

set "DLL_DIR=%~dp0..\..\lib"
set "PATH=%DLL_DIR%;%PATH%"
set "DLL_DIR=%~dp0..\..\bin"
set "PATH=%DLL_DIR%;%PATH%"

set "SCRIPT_DIR=%~dp0"
set "TARGET_SH=%SCRIPT_DIR%{{ .script_name }}"

if not exist "%TARGET_SH%" (
	echo [ERROR] Missing shell script: "%TARGET_SH%"
	exit /b 1
)

call :find_git_sh
if errorlevel 1 exit /b 1

"%GIT_SH_EXE%" "%TARGET_SH:\=/%" %*
set "CALL_EXIT_CODE=%ERRORLEVEL%"
exit /b %CALL_EXIT_CODE%

:find_git_sh
set "GIT_SH_EXE="

call :try_git_sh "%ProgramFiles%\Git\bin\sh.exe"
call :try_git_sh "%ProgramFiles%\Git\usr\bin\sh.exe"
call :try_git_sh "%ProgramFiles(x86)%\Git\bin\sh.exe"
call :try_git_sh "%ProgramFiles(x86)%\Git\usr\bin\sh.exe"
call :try_git_sh "%LocalAppData%\Programs\Git\bin\sh.exe"
call :try_git_sh "%LocalAppData%\Programs\Git\usr\bin\sh.exe"

for /f "delims=" %%I in ('where.exe git.exe 2^>nul') do (
	if not defined GIT_SH_EXE call :resolve_git_root "%%~fI"
)

for /f "delims=" %%I in ('where.exe sh.exe 2^>nul') do (
	if not defined GIT_SH_EXE set "GIT_SH_EXE=%%~fI"
)

if defined GIT_SH_EXE exit /b 0

echo [ERROR] Failed to locate Git sh.exe. Please install Git for Windows or add Git to PATH.
exit /b 1

:resolve_git_root
for %%I in ("%~1") do for %%J in ("%%~dpI..") do set "GIT_ROOT=%%~fJ"
call :try_git_sh "%GIT_ROOT%\bin\sh.exe"
call :try_git_sh "%GIT_ROOT%\usr\bin\sh.exe"
exit /b 0

:try_git_sh
if not defined GIT_SH_EXE if exist "%~1" set "GIT_SH_EXE=%~1"
exit /b 0
{{- end }}

{{- define "atapp.reload.bat" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.bat" (dict "script_name" (printf "reload_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.restart.bat" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.bat" (dict "script_name" (printf "restart_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.runcmd.bat" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.bat" (dict "script_name" (printf "runcmd_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.start.bat" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.bat" (dict "script_name" (printf "start_%s.sh" $bus_addr)) }}
{{- end }}

{{- define "atapp.stop.bat" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
{{ include "atapp.call_sh.bat" (dict "script_name" (printf "stop_%s.sh" $bus_addr)) }}
{{- end }}