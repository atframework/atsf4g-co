#Requires -Version 7.0
param(
  [Parameter(Mandatory = $true)]
  [string]$ProtocBin,

  [Parameter(Mandatory = $true)]
  [string]$OutputFile,

  [Parameter(Mandatory = $true)]
  [string]$ProtoPath,

  [Parameter(Mandatory = $true)]
  [string]$ProtoDir,

  [Parameter(Mandatory = $false)]
  [string]$ProtoFile,

  [Parameter(Mandatory = $false)]
  [switch]$IncludeImports
)

$ErrorActionPreference = 'Stop'

# Multiple values arrive as one ';'-separated string: native -File invocation cannot bind
# space-separated arguments to array parameters.
$protoPaths = @($ProtoPath -split ';' | Where-Object { $_ })
$protoDirs = @($ProtoDir -split ';' | Where-Object { $_ })
$protoFiles = @()
if ($ProtoFile) {
  $protoFiles = @($ProtoFile -split ';' | Where-Object { $_ })
}

$outDir = Split-Path -Parent $OutputFile
if ($outDir -and -not (Test-Path $outDir)) {
  New-Item -ItemType Directory -Force $outDir | Out-Null
}

$protocArgs = @()
if ($IncludeImports) {
  $protocArgs += '--include_imports'
}
foreach ($path in $protoPaths) {
  $protocArgs += "--proto_path=$path"
}

# Expand each required proto group and keep the caller-provided (relative) path form,
# so generated descriptors stay identical to the POSIX glob expansion.
$files = @()
foreach ($dir in $protoDirs) {
  $expanded = @(Get-ChildItem -File -Name (Join-Path $dir '*.proto') | Sort-Object)
  if ($expanded.Count -eq 0) {
    Write-Error "No .proto files found in required group: $dir"
  }
  foreach ($name in $expanded) {
    $files += "$dir/$name"
  }
}
$files += $protoFiles

& $ProtocBin @protocArgs @files -o $OutputFile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
