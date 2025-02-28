$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'

$OutputEncoding = [System.Text.UTF8Encoding]::new()

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Definition

Set-Location $SCRIPT_DIR

Remove-Item -Recurse -Force third_party/install
Remove-Item -Recurse -Force third_party/packages

if (Test-Path "build") {
  Remove-Item -Recurse -Force "build"
}

foreach ($item in Get-ChildItem -Filter "build_*" -Depth 0 -Directory) {
  Remove-Item -Recurse -Force $item
}

foreach ($item in Get-ChildItem -Filter ".mako_modules*" -Recurse -Directory) {
  Remove-Item -Recurse -Force $item
}

foreach ($item in Get-ChildItem -Filter ".jinja2_modules*" -Recurse -Directory) {
  Remove-Item -Recurse -Force $item
}

foreach ($item in Get-ChildItem -Filter "__pycache__" -Recurse -Directory) {
  Remove-Item -Recurse -Force $item
}

git submodule foreach --recursive git clean -dfx
