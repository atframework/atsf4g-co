# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

#requires -Version 7.0

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $RepositoryRoot,

  [Parameter(Mandatory = $true)]
  [string] $BuildDirectory,

  [Parameter(Mandatory = $true)]
  [string] $ClangTidyExecutable,

  [Parameter(Mandatory = $true)]
  [ValidateRange(0, [int]::MaxValue)]
  [int] $MaxIssues,

  [Parameter(Mandatory = $true)]
  [ValidateRange(1, [int]::MaxValue)]
  [int] $ClangTidyMajorVersion,

  [string] $PythonExecutable = '',

  [string] $PrepareScript = '',

  [Parameter(Mandatory = $true)]
  [string] $ReportDirectory,

  [switch] $PrepareMsvcCompilationDatabase
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

function Invoke-GitNameList {
  param(
    [Parameter(Mandatory = $true)]
    [System.Management.Automation.ApplicationInfo] $GitCommand,

    [Parameter(Mandatory = $true)]
    [string] $WorkingDirectory,

    [Parameter(Mandatory = $true)]
    [string[]] $Arguments
  )

  $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $GitCommand.Source
  $startInfo.UseShellExecute = $false
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $startInfo.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
  $startInfo.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)
  $startInfo.ArgumentList.Add('-C')
  $startInfo.ArgumentList.Add($WorkingDirectory)
  foreach ($argument in $Arguments) {
    $startInfo.ArgumentList.Add($argument)
  }

  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $startInfo
  if (-not $process.Start()) {
    throw "Failed to start git: $($GitCommand.Source)"
  }

  $standardOutput = $process.StandardOutput.ReadToEndAsync()
  $standardError = $process.StandardError.ReadToEndAsync()
  $process.WaitForExit()
  $output = $standardOutput.GetAwaiter().GetResult()
  $errorOutput = $standardError.GetAwaiter().GetResult()
  if ($process.ExitCode -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $($process.ExitCode): $errorOutput"
  }

  return @($output.Split([char] 0, [System.StringSplitOptions]::RemoveEmptyEntries))
}

if (-not (Test-Path -LiteralPath $RepositoryRoot -PathType Container)) {
  throw "Repository root does not exist: $RepositoryRoot"
}
$RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path

$gitMetadataPath = Join-Path -Path $RepositoryRoot -ChildPath '.git'
if (-not (Test-Path -LiteralPath $gitMetadataPath)) {
  Write-Output "clang-tidy: skipped because '$RepositoryRoot' is not a Git work tree."
  exit 0
}

$gitCommand = Get-Command -Name 'git' -CommandType Application -ErrorAction SilentlyContinue |
  Select-Object -First 1
if ($null -eq $gitCommand) {
  Write-Output 'clang-tidy: skipped because git is not available.'
  exit 0
}

$insideWorkTree = & $gitCommand.Source '-C' $RepositoryRoot 'rev-parse' '--is-inside-work-tree' 2>$null
if ($LASTEXITCODE -ne 0 -or $insideWorkTree -notcontains 'true') {
  Write-Output "clang-tidy: skipped because '$RepositoryRoot' is not a Git work tree."
  exit 0
}

$changedFileSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$gitNameListParameters = @{
  GitCommand = $gitCommand
  WorkingDirectory = $RepositoryRoot
  Arguments = @('diff', '--name-only', '-z', '--diff-filter=ACMRTUXB', '--')
}
foreach ($path in Invoke-GitNameList @gitNameListParameters) {
  [void] $changedFileSet.Add($path)
}
$gitNameListParameters.Arguments = @('diff', '--cached', '--name-only', '-z', '--diff-filter=ACMRTUXB', '--')
foreach ($path in Invoke-GitNameList @gitNameListParameters) {
  [void] $changedFileSet.Add($path)
}
& $gitCommand.Source '-C' $RepositoryRoot 'rev-parse' '--verify' '--quiet' '@{upstream}^{commit}' 2>$null |
  Out-Null
if ($LASTEXITCODE -eq 0) {
  $gitNameListParameters.Arguments =
    @('diff', '--name-only', '-z', '--diff-filter=ACMRTUXB', '@{upstream}...HEAD', '--')
  foreach ($path in Invoke-GitNameList @gitNameListParameters) {
    [void] $changedFileSet.Add($path)
  }
}

$clangTidyExtensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($extension in @('.c', '.c++', '.cc', '.cpp', '.cu', '.cuh', '.cxx', '.h', '.h++', '.hh', '.hpp', '.hxx')) {
  [void] $clangTidyExtensions.Add($extension)
}

$changedFiles = @(
  foreach ($relativePath in $changedFileSet) {
    if (-not $clangTidyExtensions.Contains([System.IO.Path]::GetExtension($relativePath))) {
      continue
    }
    $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $relativePath
    if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
      [PSCustomObject]@{
        FullPath = $fullPath
        RelativePath = $relativePath.Replace('\', '/')
      }
    }
  }
) | Sort-Object -Property RelativePath
$changedFiles = @($changedFiles)

if ($changedFiles.Count -eq 0) {
  Write-Output 'clang-tidy: no staged, unstaged, or unpushed C/C++ files to check.'
  exit 0
}

if (-not (Test-Path -LiteralPath $BuildDirectory -PathType Container)) {
  [Console]::Error.WriteLine("clang-tidy: build directory does not exist: $BuildDirectory")
  exit 2
}
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$compilationDatabase = Join-Path -Path $BuildDirectory -ChildPath 'compile_commands.json'
if (-not (Test-Path -LiteralPath $compilationDatabase -PathType Leaf)) {
  [Console]::Error.WriteLine("clang-tidy: compilation database does not exist: $compilationDatabase")
  exit 2
}

$clangTidyCommand = Get-Command -Name $ClangTidyExecutable -CommandType Application -ErrorAction SilentlyContinue |
  Select-Object -First 1
if ($null -eq $clangTidyCommand) {
  [Console]::Error.WriteLine("clang-tidy: executable is no longer available: $ClangTidyExecutable")
  exit 2
}

if (-not (Test-Path -LiteralPath $ReportDirectory -PathType Container)) {
  [void] (New-Item -ItemType Directory -Path $ReportDirectory -Force)
}
$analysisDirectory = Join-Path -Path $ReportDirectory -ChildPath 'current'
if (-not (Test-Path -LiteralPath $analysisDirectory -PathType Container)) {
  [void] (New-Item -ItemType Directory -Path $analysisDirectory -Force)
}

$analysisBuildDirectory = $BuildDirectory
if ($PrepareMsvcCompilationDatabase) {
  if ([string]::IsNullOrWhiteSpace($PythonExecutable) -or [string]::IsNullOrWhiteSpace($PrepareScript)) {
    [Console]::Error.WriteLine(
      'clang-tidy: -PythonExecutable and -PrepareScript are required with -PrepareMsvcCompilationDatabase.'
    )
    exit 2
  }
  $pythonCommand = Get-Command -Name $PythonExecutable -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($null -eq $pythonCommand) {
    [Console]::Error.WriteLine("clang-tidy: Python executable is no longer available: $PythonExecutable")
    exit 2
  }
  if (-not (Test-Path -LiteralPath $PrepareScript -PathType Leaf)) {
    [Console]::Error.WriteLine("clang-tidy: compilation database preparation script does not exist: $PrepareScript")
    exit 2
  }

  $preparedCompilationDatabase = Join-Path -Path $analysisDirectory -ChildPath 'compile_commands.json'
  & $pythonCommand.Source $PrepareScript '--input' $compilationDatabase '--output' $preparedCompilationDatabase `
    '--remove-cmake-msvc-pch'
  if ($LASTEXITCODE -ne 0) {
    [Console]::Error.WriteLine('clang-tidy: failed to prepare the analysis-only compilation database.')
    exit 2
  }
  $analysisBuildDirectory = $analysisDirectory
}

$configurationArguments = @()
$clangTidyConfiguration = Join-Path -Path $RepositoryRoot -ChildPath '.clang-tidy'
if ($ClangTidyMajorVersion -lt 19 -and (Test-Path -LiteralPath $clangTidyConfiguration -PathType Leaf)) {
  $compatibleConfiguration = Join-Path -Path $analysisDirectory -ChildPath '.clang-tidy'
  $configurationLines = [System.IO.File]::ReadAllLines($clangTidyConfiguration) |
    Where-Object { $_ -notmatch '^ExcludeHeaderFilterRegex[\t ]*:' }
  [System.IO.File]::WriteAllLines(
    $compatibleConfiguration,
    $configurationLines,
    [System.Text.UTF8Encoding]::new($false)
  )
  $configurationArguments += "--config-file=$compatibleConfiguration"
  Write-Output (
    'clang-tidy: using a compatibility configuration without ExcludeHeaderFilterRegex, which requires clang-tidy 19.'
  )
}

$reportLines = [System.Collections.Generic.List[string]]::new()
$issueCount = 0
$diagnosticPattern =
  '^(?:.+):[0-9]+:[0-9]+:\s+(?:warning|error|fatal error):|^.+\([0-9]+(?:,[0-9]+)?\):\s+(?:warning|error|fatal error)\b'
foreach ($changedFile in $changedFiles) {
  $lineFilter = [ordered]@{ name = $changedFile.RelativePath } |
    ConvertTo-Json -Compress -AsArray
  $clangTidyArguments = @(
    '--quiet'
    '--use-color=false'
    "-p=$analysisBuildDirectory"
    '--extra-arg=-Wno-error'
    "--line-filter=$lineFilter"
  )
  $clangTidyArguments += $configurationArguments
  if ($PrepareMsvcCompilationDatabase) {
    $clangTidyArguments += @(
      '--extra-arg=/WX-'
      '--extra-arg=-Wno-unused-command-line-argument'
    )
  }
  $clangTidyArguments += $changedFile.FullPath

  $lintOutput = @(& $clangTidyCommand.Source @clangTidyArguments 2>&1) |
    ForEach-Object { $_.ToString() }
  $lintExitCode = $LASTEXITCODE
  $reportLines.Add("clang-tidy: $($changedFile.FullPath)")
  foreach ($line in $lintOutput) {
    $reportLines.Add($line)
  }

  if ($lintExitCode -ne 0) {
    [Console]::Error.WriteLine(($lintOutput -join [Environment]::NewLine))
    [Console]::Error.WriteLine(
      "clang-tidy: failed to analyze '$($changedFile.FullPath)' with exit code $lintExitCode."
    )
    exit 2
  }

  $issueCount += @($lintOutput | Select-String -Pattern $diagnosticPattern).Count
}

if ($issueCount -gt $MaxIssues) {
  [Console]::Error.WriteLine("clang-tidy report for $($changedFiles.Count) staged, unstaged, or unpushed C/C++ file(s):")
  [Console]::Error.WriteLine(($reportLines -join [Environment]::NewLine))
  [Console]::Error.WriteLine("clang-tidy: $issueCount issue(s) exceed the configured maximum of $MaxIssues.")
  exit 1
}

Write-Output (
  "clang-tidy: $issueCount issue(s) found in $($changedFiles.Count) staged, unstaged, or unpushed C/C++ file(s); " +
  "maximum allowed is $MaxIssues."
)
exit 0
