@echo off
cd /d %~dp0

set "HELM_DIR=%~dp0..\..\tools\helm"
set "PATH=%HELM_DIR%;%PATH%"

for /d %%i in (..\..\cloud-native\charts\*) do (
    if "%%i" neq "libapp" (
        if "%%i" neq "app" (
            echo "%%i"
            helm dependency update %%i
        )
    )
)