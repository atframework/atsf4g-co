#Requires -Version 7.0
param(
  [Parameter(Mandatory = $true)]
  [string]$ProtocBin,

  [Parameter(Mandatory = $true)]
  [string]$ProtoPath,

  [Parameter(Mandatory = $true)]
  [string]$ProtoDir,

  [Parameter(Mandatory = $true)]
  [string]$StagingDir,

  [Parameter(Mandatory = $false)]
  [string]$PublishRoot = '.'
)

$ErrorActionPreference = 'Stop'

# Multiple values arrive as one ';'-separated string: native -File invocation cannot bind
# space-separated arguments to array parameters.
$protoPaths = @($ProtoPath -split ';' | Where-Object { $_ })
$protoDirs = @($ProtoDir -split ';' | Where-Object { $_ })

# Guards: refuse empty values and filesystem roots before any recursive delete.
foreach ($path in @($StagingDir, $PublishRoot)) {
  if ([string]::IsNullOrWhiteSpace($path)) {
    Write-Error 'StagingDir/PublishRoot must not be empty'
  }
  $full = [IO.Path]::GetFullPath($path)
  if ($full -eq [IO.Path]::GetPathRoot($full)) {
    Write-Error "Refusing unsafe directory: $path"
  }
}

$staging = [IO.Path]::GetFullPath($StagingDir)
if (Test-Path $staging) {
  Remove-Item -Recurse -Force $staging
}
New-Item -ItemType Directory -Force $staging | Out-Null

# protoc-gen-go / protoc-gen-go-mutable live in GOPATH/bin
$goPath = & go env GOPATH
$env:PATH = (Join-Path $goPath 'bin') + [IO.Path]::PathSeparator + $env:PATH

$baseArgs = @(
  "--go_out=$staging",
  "--mutable_out=$staging",
  '--go_opt=paths=source_relative',
  '--mutable_opt=paths=source_relative'
)
foreach ($path in $protoPaths) {
  $baseArgs += "--proto_path=$path"
}

# One protoc invocation per group, mirroring the POSIX flow; keep relative path form
# so outputs land in the same source-relative locations below the staging root.
foreach ($dir in $protoDirs) {
  $expanded = @(Get-ChildItem -File -Name (Join-Path $dir '*.proto') | Sort-Object)
  if ($expanded.Count -eq 0) {
    Write-Error "No .proto files found in required group: $dir"
  }
  $files = @()
  foreach ($name in $expanded) {
    $files += "$dir/$name"
  }
  & $ProtocBin @baseArgs @files
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# protoc rewrites outputs unconditionally; publish content-stably so unchanged
# generated files keep their timestamps. Scope the mirror to the generated
# protocol/ subtree only, so sibling files (Taskfile.yml etc.) are never touched.
& (Join-Path $PSScriptRoot 'publish-directory.ps1') -SourceDir (Join-Path $staging 'protocol') -TargetDir (Join-Path $PublishRoot 'protocol')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
