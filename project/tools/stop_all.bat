@echo off
setlocal
REM Wrapper for stop_all.ps1 in the deploy base directory.
set "PWSH_CMD=pwsh"
where pwsh >nul 2>nul || set "PWSH_CMD=powershell"
"%PWSH_CMD%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0stop_all.ps1" %*
exit /b %ERRORLEVEL%