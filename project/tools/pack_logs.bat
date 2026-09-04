@echo off
setlocal EnableDelayedExpansion

REM ============================================================
REM  pack_logs.bat - Pack all "*/log" directories (except otelcol/log)
REM  under the deploy base directory into a timestamped 7z archive.
REM  Archive uses store mode (-mx0): no compression, just copy.
REM ============================================================

set "DEPLOY_BASE=%~dp0"
cd /d "%DEPLOY_BASE%"

REM ---- Locate 7z executable ----
REM "%ProgramFiles(x86)%" expands to a path containing parentheses, which breaks
REM a nested `if` inside a parenthesized block. Use goto labels instead.
set "SZ_CMD="
where 7z >nul 2>nul && set "SZ_CMD=7z"
if defined SZ_CMD goto :found_7z
if exist "%ProgramFiles%\7-Zip\7z.exe" set "SZ_CMD=%ProgramFiles%\7-Zip\7z.exe"
if defined SZ_CMD goto :found_7z
if exist "%ProgramFiles(x86)%\7-Zip\7z.exe" set "SZ_CMD=%ProgramFiles(x86)%\7-Zip\7z.exe"
if defined SZ_CMD goto :found_7z
echo [ERROR] 7-Zip (7z.exe) not found. Install 7-Zip or add it to PATH.
exit /b 1
:found_7z

REM ---- Collect all "*/log" directories except otelcol/log ----
set "ARGS="
for /d %%D in (*) do (
    if /i not "%%D"=="otelcol" (
        if exist "%%D\log" (
            set "ARGS=!ARGS! "%%D\log""
        )
    )
)

if not defined ARGS (
    echo [INFO] No log directories found under "%DEPLOY_BASE%".
    exit /b 0
)

REM ---- Timestamp as archive name: yyyyMMdd_HHmmss ----
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format 'yyyyMMdd_HHmmss'"`) do set "TS=%%i"
set "ARCHIVE=%TS%.7z"

echo [INFO] Packing log directories into "%ARCHIVE%" (store mode)...
REM -ssw lets 7z read log files that are still open for writing by running services.
"%SZ_CMD%" a -t7z -mx0 -ssw "%ARCHIVE%" !ARGS!
set "SZ_EXIT=!errorlevel!"
if !SZ_EXIT! GEQ 2 (
    echo [ERROR] 7z failed with exit code !SZ_EXIT!
    exit /b !SZ_EXIT!
)
if !SZ_EXIT! EQU 1 (
    echo [WARN] 7z completed with warnings; some log files may be locked by running services.
)
echo [INFO] Done: "%ARCHIVE%"
exit /b 0