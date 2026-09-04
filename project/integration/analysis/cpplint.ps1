# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

#requires -Version 7.0

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $RepositoryRoot,

  [Parameter(Mandatory = $true)]
  [AllowEmptyString()]
  [string] $PythonVenvDir,

  [Parameter(Mandatory = $true)]
  [ValidateRange(0, [int]::MaxValue)]
  [int] $MaxIssues
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
  Write-Output "cpplint: skipped because '$RepositoryRoot' is not a Git work tree."
  exit 0
}

$gitCommand = Get-Command -Name 'git' -CommandType Application -ErrorAction SilentlyContinue |
  Select-Object -First 1
if ($null -eq $gitCommand) {
  Write-Output 'cpplint: skipped because git is not available.'
  exit 0
}

$insideWorkTree = & $gitCommand.Source '-C' $RepositoryRoot 'rev-parse' '--is-inside-work-tree' 2>$null
if ($LASTEXITCODE -ne 0 -or $insideWorkTree -notcontains 'true') {
  Write-Output "cpplint: skipped because '$RepositoryRoot' is not a Git work tree."
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

$cpplintExtensions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($extension in @('.c', '.c++', '.cc', '.cpp', '.cu', '.cuh', '.cxx', '.h', '.h++', '.hh', '.hpp', '.hxx')) {
  [void] $cpplintExtensions.Add($extension)
}

$changedFiles = @(
  foreach ($relativePath in $changedFileSet) {
    if (-not $cpplintExtensions.Contains([System.IO.Path]::GetExtension($relativePath))) {
      continue
    }
    $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $relativePath
    if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
      $fullPath
    }
  }
) | Sort-Object
$changedFiles = @($changedFiles)

if ($changedFiles.Count -eq 0) {
  Write-Output 'cpplint: no staged, unstaged, or unpushed C/C++ files to check.'
  exit 0
}

if (-not [string]::IsNullOrWhiteSpace($PythonVenvDir)) {
  $env:VIRTUAL_ENV = $PythonVenvDir
  $venvPathEntries = @(
    Join-Path -Path $PythonVenvDir -ChildPath 'Scripts'
    Join-Path -Path $PythonVenvDir -ChildPath 'bin'
  )
  $env:PATH = (@($venvPathEntries) + @($env:PATH)) -join [System.IO.Path]::PathSeparator
}

$cpplintCommand = Get-Command -Name 'cpplint' -CommandType Application, ExternalScript -ErrorAction SilentlyContinue |
  Select-Object -First 1
if ($null -eq $cpplintCommand) {
  [Console]::Error.WriteLine(
    'cpplint: executable not found in the project Python virtual environment or inherited PATH.'
  )
  exit 2
}

$reportLines = [System.Collections.Generic.List[string]]::new()
$issueCount = 0
foreach ($changedFile in $changedFiles) {
  $lintOutput = @(& $cpplintCommand.Source "--repository=$RepositoryRoot" $changedFile 2>&1) |
    ForEach-Object { $_.ToString() }
  $lintExitCode = $LASTEXITCODE
  foreach ($line in $lintOutput) {
    $reportLines.Add($line)
  }

  $issueCountMatch = $lintOutput |
    Select-String -Pattern '^Total errors found:\s*([0-9]+)\s*$' |
    Select-Object -Last 1
  if ($null -eq $issueCountMatch) {
    if ($lintExitCode -ne 0) {
      [Console]::Error.WriteLine(($lintOutput -join [Environment]::NewLine))
      [Console]::Error.WriteLine("cpplint: failed to analyze '$changedFile' with exit code $lintExitCode.")
      exit 2
    }
  } else {
    $issueCount += [int] $issueCountMatch.Matches[0].Groups[1].Value
  }
}

if ($issueCount -gt $MaxIssues) {
  [Console]::Error.WriteLine("cpplint report for $($changedFiles.Count) staged, unstaged, or unpushed C/C++ file(s):")
  [Console]::Error.WriteLine(($reportLines -join [Environment]::NewLine))
  [Console]::Error.WriteLine("cpplint: $issueCount issue(s) exceed the configured maximum of $MaxIssues.")
  exit 1
}

Write-Output (
  "cpplint: $issueCount issue(s) found in $($changedFiles.Count) staged, unstaged, or unpushed C/C++ file(s); " +
  "maximum allowed is $MaxIssues."
)
exit 0
