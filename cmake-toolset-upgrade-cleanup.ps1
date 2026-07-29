$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'

$OutputEncoding = [System.Text.UTF8Encoding]::new()

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Definition

Set-Location $SCRIPT_DIR


if (!(Test-Path "atframework/cmake-toolset") -or !(Test-Path "third_party/install")) {
  Exit 0
}

Set-Location "atframework/cmake-toolset"
$CMAKE_TOOLSET_VERSION = & git rev-parse --short HEAD
Set-Location $SCRIPT_DIR

$CMAKE_VERSION_FILES = Get-ChildItem -Include ".cmake-toolse.version" -Recurse -Depth 2 -File -Hidden -Path third_party/install

foreach ($CMAKE_VERSION_FILE in $CMAKE_VERSION_FILES) {
  $CMAKE_VERSION = Get-Content $CMAKE_VERSION_FILE
  if ($CMAKE_VERSION.Substring(0, $CMAKE_TOOLSET_VERSION.Length) -ne $CMAKE_TOOLSET_VERSION) {
    & pwsh -File cleanup-prebuilts.ps1
    break
  }
}
