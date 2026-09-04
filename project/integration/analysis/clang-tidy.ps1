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

  [ValidateRange(1, [int]::MaxValue)]
  [int] $Jobs = 4,

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
  GitCommand       = $gitCommand
  WorkingDirectory = $RepositoryRoot
  Arguments        = @('diff', '--name-only', '-z', '--diff-filter=ACMRTUXB', '--')
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
        FullPath     = $fullPath
        RelativePath = $relativePath.Replace('\', '/')
      }
    }
  }
) | Sort-Object -Property RelativePath
$changedFiles = @($changedFiles)

# Tab characters cannot be carried by the rules manifest TSV; match the bash twin: warn and skip
$tabbedFiles = @($changedFiles | Where-Object { $_.RelativePath.Contains("`t") })
foreach ($tabbedFile in $tabbedFiles) {
  [Console]::Error.WriteLine(
    "clang-tidy: warning: paths with tab characters are not supported, skipped: $($tabbedFile.RelativePath)"
  )
}
if ($tabbedFiles.Count -gt 0) {
  $changedFiles = @($changedFiles | Where-Object { -not $_.RelativePath.Contains("`t") })
}

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
  $configurationLines = @([System.IO.File]::ReadAllLines($clangTidyConfiguration) |
    Where-Object { $_ -notmatch '^ExcludeHeaderFilterRegex[\t ]*:' })
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

# Translate the repository .clangd Diagnostics.ClangTidy rules into per-file actions so the analysis
# matches the editor behavior (for example clang-analyzer-* removal and third-party skips).
$fileRules = @{}
$clangdConfiguration = Join-Path -Path $RepositoryRoot -ChildPath '.clangd'
if (Test-Path -LiteralPath $clangdConfiguration -PathType Leaf) {
  $clangdRulesScript = Join-Path -Path $PSScriptRoot -ChildPath 'clang-tidy-clangd-rules.py'
  $clangdPythonCommand = $null
  if (-not [string]::IsNullOrWhiteSpace($PythonExecutable)) {
    $clangdPythonCommand = Get-Command -Name $PythonExecutable -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
  }
  if (($null -ne $clangdPythonCommand) -and (Test-Path -LiteralPath $clangdRulesScript -PathType Leaf)) {
    $rulesInput = Join-Path -Path $analysisDirectory -ChildPath 'analysis-files.txt'
    $rulesManifest = Join-Path -Path $analysisDirectory -ChildPath 'clangd-rules.tsv'
    [System.IO.File]::WriteAllLines(
      $rulesInput,
      [string[]] @($changedFiles | ForEach-Object { $_.RelativePath }),
      [System.Text.UTF8Encoding]::new($false)
    )
    $rulesArguments = @(
      $clangdRulesScript
      '--clangd-config'
      $clangdConfiguration
      '--files'
      $rulesInput
      '--manifest'
      $rulesManifest
    )
    & $clangdPythonCommand.Source @rulesArguments
    if ($LASTEXITCODE -eq 0) {
      foreach ($ruleLine in [System.IO.File]::ReadLines($rulesManifest)) {
        if ([string]::IsNullOrEmpty($ruleLine)) {
          continue
        }
        $ruleFields = $ruleLine.Split("`t")
        if ($ruleFields.Count -lt 2) {
          continue
        }
        $fileRules[$ruleFields[0]] = [PSCustomObject]@{
          Action      = $ruleFields[1]
          ExtraChecks = $(if ($ruleFields.Count -gt 2) { $ruleFields[2] } else { '' })
        }
      }
    }
    else {
      [Console]::Error.WriteLine('clang-tidy: warning: failed to evaluate .clangd rules; analyzing without them.')
    }
  }
  else {
    [Console]::Error.WriteLine(
      'clang-tidy: warning: .clangd exists but no Python interpreter is available; .clangd rules are not applied.'
    )
  }
}

$fileIndex = 0
foreach ($changedFile in $changedFiles) {
  $fileAction = 'analyze'
  $fileExtraChecks = ''
  if ($fileRules.ContainsKey($changedFile.RelativePath)) {
    $fileAction = $fileRules[$changedFile.RelativePath].Action
    $fileExtraChecks = $fileRules[$changedFile.RelativePath].ExtraChecks
  }
  $changedFile | Add-Member -NotePropertyName 'Index' -NotePropertyValue $fileIndex
  $changedFile | Add-Member -NotePropertyName 'Action' -NotePropertyValue $fileAction
  $changedFile | Add-Member -NotePropertyName 'ExtraChecks' -NotePropertyValue $fileExtraChecks
  $fileIndex += 1
}

# Worker results are single-run artifacts: the analysis directory persists across runs, so remove
# leftovers from interrupted runs up front to keep a missing worker result fail-closed, and clean
# them again after aggregation to avoid unbounded accumulation.
Get-ChildItem -LiteralPath $analysisDirectory -Filter 'worker-*' -File -ErrorAction SilentlyContinue |
Remove-Item -Force -ErrorAction SilentlyContinue

# Each worker analyzes one file and stores the clang-tidy output and exit code under the analysis
# directory, so the main thread can aggregate results in the deterministic file order.
$clangTidyExecutablePath = $clangTidyCommand.Source
$changedFiles | ForEach-Object -ThrottleLimit $Jobs -Parallel {
  $workerFile = $_
  $workerAnalysisDirectory = $using:analysisDirectory
  $workerBuildDirectory = $using:analysisBuildDirectory
  $workerClangTidyExecutable = $using:clangTidyExecutablePath
  $workerConfigurationArguments = $using:configurationArguments
  $workerPrepareMsvc = [bool] $using:PrepareMsvcCompilationDatabase
  $workerOutputFile = Join-Path -Path $workerAnalysisDirectory -ChildPath "worker-$($workerFile.Index).out"
  $workerCodeFile = Join-Path -Path $workerAnalysisDirectory -ChildPath "worker-$($workerFile.Index).code"
  $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
  try {
    if ($workerFile.Action -eq 'skip') {
      [System.IO.File]::WriteAllLines(
        $workerOutputFile,
        [string[]] @('clang-tidy: skipped because .clangd rules remove all checks for this file.'),
        $utf8NoBom
      )
      [System.IO.File]::WriteAllLines($workerCodeFile, [string[]] @('0'), $utf8NoBom)
      return
    }
    $workerClangTidyArguments = @(
      '--quiet'
      '--use-color=false'
      "-p=$workerBuildDirectory"
      '--extra-arg=-Wno-error'
    )
    $workerLineFilter = [ordered]@{ name = $workerFile.RelativePath } | ConvertTo-Json -Compress -AsArray
    $workerClangTidyArguments += "--line-filter=$workerLineFilter"
    $workerClangTidyArguments += $workerConfigurationArguments
    if (-not [string]::IsNullOrEmpty($workerFile.ExtraChecks)) {
      $workerClangTidyArguments += "--checks=$($workerFile.ExtraChecks)"
    }
    if ($workerPrepareMsvc) {
      $workerClangTidyArguments += @(
        '--extra-arg=/WX-'
        '--extra-arg=-Wno-unused-command-line-argument'
      )
    }
    $workerClangTidyArguments += $workerFile.FullPath

    $lintOutput = @(& $workerClangTidyExecutable @workerClangTidyArguments 2>&1 |
      ForEach-Object { $_.ToString() })
    $lintExitCode = $LASTEXITCODE
    [System.IO.File]::WriteAllLines($workerOutputFile, [string[]] $lintOutput, $utf8NoBom)
    [System.IO.File]::WriteAllLines($workerCodeFile, [string[]] @([string] $lintExitCode), $utf8NoBom)
  }
  catch {
    [System.IO.File]::WriteAllLines(
      $workerOutputFile,
      [string[]] @("clang-tidy: worker error: $($_.Exception.Message)"),
      $utf8NoBom
    )
    [System.IO.File]::WriteAllLines($workerCodeFile, [string[]] @('125'), $utf8NoBom)
  }
}

$skippedCount = 0
foreach ($changedFile in $changedFiles) {
  $workerOutputFile = Join-Path -Path $analysisDirectory -ChildPath "worker-$($changedFile.Index).out"
  $workerCodeFile = Join-Path -Path $analysisDirectory -ChildPath "worker-$($changedFile.Index).code"
  if (-not (Test-Path -LiteralPath $workerCodeFile -PathType Leaf)) {
    [Console]::Error.WriteLine(
      "clang-tidy: failed to analyze '$($changedFile.FullPath)' (missing worker result)."
    )
    exit 2
  }
  $lintExitCodeText = [System.IO.File]::ReadAllLines($workerCodeFile) | Select-Object -First 1
  $lintExitCode = 0
  if (-not [int]::TryParse("$lintExitCodeText", [ref] $lintExitCode)) {
    $lintExitCode = 125
  }
  $lintOutput = @([System.IO.File]::ReadAllLines($workerOutputFile))
  if ($changedFile.Action -eq 'skip') {
    $skippedCount += 1
  }

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

Get-ChildItem -LiteralPath $analysisDirectory -Filter 'worker-*' -File -ErrorAction SilentlyContinue |
Remove-Item -Force -ErrorAction SilentlyContinue

if ($issueCount -gt $MaxIssues) {
  [Console]::Error.WriteLine("clang-tidy report for $($changedFiles.Count) staged, unstaged, or unpushed C/C++ file(s):")
  [Console]::Error.WriteLine(($reportLines -join [Environment]::NewLine))
  [Console]::Error.WriteLine("clang-tidy: $issueCount issue(s) exceed the configured maximum of $MaxIssues.")
  exit 1
}

if ($skippedCount -gt 0) {
  Write-Output (
    "clang-tidy: $issueCount issue(s) found in $($changedFiles.Count - $skippedCount) analyzed C/C++ file(s) " +
    "($skippedCount skipped by .clangd rules); maximum allowed is $MaxIssues."
  )
}
else {
  Write-Output (
    "clang-tidy: $issueCount issue(s) found in $($changedFiles.Count) staged, unstaged, or unpushed C/C++ file(s); " +
    "maximum allowed is $MaxIssues."
  )
}
exit 0
