@echo off
setlocal
REM Wrapper for tools\script\start_local_test_env.ps1 in the deploy base directory.
set "PWSH_CMD=pwsh"
where pwsh >nul 2>nul || set "PWSH_CMD=powershell"
"%PWSH_CMD%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\script\start_local_test_env.ps1" %*
exit /b %ERRORLEVEL%