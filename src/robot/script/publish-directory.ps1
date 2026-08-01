#Requires -Version 7.0
param(
  [Parameter(Mandatory = $true)]
  [string]$SourceDir,

  [Parameter(Mandatory = $true)]
  [string]$TargetDir
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SourceDir) -or [string]::IsNullOrWhiteSpace($TargetDir)) {
  Write-Error 'SourceDir/TargetDir must not be empty'
}

$source = [IO.Path]::GetFullPath($SourceDir)
$target = [IO.Path]::GetFullPath($TargetDir)

if (-not (Test-Path -LiteralPath $source -PathType Container)) {
  Write-Error "SourceDir not found: $SourceDir"
}
if ($source -eq [IO.Path]::GetPathRoot($source) -or $target -eq [IO.Path]::GetPathRoot($target)) {
  Write-Error "Refusing unsafe source or target: $SourceDir -> $TargetDir"
}
if ($source -eq $target) {
  Write-Error 'SourceDir and TargetDir must differ'
}

if (-not (Test-Path -LiteralPath $target)) {
  New-Item -ItemType Directory -Force $target | Out-Null
}

# Publish only files whose content actually changed, keeping untouched outputs timestamp-stable.
$copied = 0
foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File) {
  $rel = [IO.Path]::GetRelativePath($source, $file.FullName)
  $dest = Join-Path $target $rel
  $destDir = Split-Path -Parent $dest
  if (-not (Test-Path $destDir)) {
    New-Item -ItemType Directory -Force $destDir | Out-Null
  }
  if (-not (Test-Path -LiteralPath $dest) -or
      (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne
      (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash) {
    Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
    $copied++
  }
}

# Remove stale files that belong to this flow (mirror semantics), after the target-root guard above.
$removed = 0
foreach ($file in Get-ChildItem -LiteralPath $target -Recurse -File) {
  $rel = [IO.Path]::GetRelativePath($target, $file.FullName)
  if (-not (Test-Path -LiteralPath (Join-Path $source $rel))) {
    Remove-Item -LiteralPath $file.FullName -Force
    $removed++
  }
}

Write-Output "Published $copied file(s), removed $removed stale file(s) -> $target"
