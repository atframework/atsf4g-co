#!/bin/sh
# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

repository_root=''
build_directory=''
clang_tidy_executable=''
max_issues=''
report_directory=''
clang_tidy_major_version=''
python_executable=''
prepare_script=''
prepare_msvc_compilation_database=0
jobs=4

print_usage() {
  printf '%s\n' \
    'Usage: clang-tidy.sh --repository-root <path> --build-directory <path> --clang-tidy-executable <path>' \
    '       --max-issues <count> --clang-tidy-major-version <version> --report-directory <path>' \
    '       [--jobs <count>] [--python-executable <path> --prepare-script <path>]' \
    '       [--prepare-msvc-compilation-database]'
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --repository-root)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      repository_root=$2
      shift 2
      ;;
    --build-directory)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      build_directory=$2
      shift 2
      ;;
    --clang-tidy-executable)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      clang_tidy_executable=$2
      shift 2
      ;;
    --max-issues)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      max_issues=$2
      shift 2
      ;;
    --report-directory)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      report_directory=$2
      shift 2
      ;;
    --clang-tidy-major-version)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      clang_tidy_major_version=$2
      shift 2
      ;;
    --python-executable)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      python_executable=$2
      shift 2
      ;;
    --prepare-script)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      prepare_script=$2
      shift 2
      ;;
    --prepare-msvc-compilation-database)
      prepare_msvc_compilation_database=1
      shift
      ;;
    --jobs)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      jobs=$2
      shift 2
      ;;
    *)
      printf 'clang-tidy: unknown argument: %s\n' "$1" >&2
      print_usage >&2
      exit 2
      ;;
  esac
done

if [ -z "$repository_root" ] || [ -z "$build_directory" ] || [ -z "$clang_tidy_executable" ] ||
   [ -z "$max_issues" ] || [ -z "$clang_tidy_major_version" ] || [ -z "$report_directory" ]; then
  print_usage >&2
  exit 2
fi
case "$max_issues" in
  *[!0-9]*|'')
    printf 'clang-tidy: --max-issues must be a non-negative integer, got: %s\n' "$max_issues" >&2
    exit 2
    ;;
esac
case "$clang_tidy_major_version" in
  *[!0-9]*|'')
    printf 'clang-tidy: --clang-tidy-major-version must be a positive integer, got: %s\n' \
      "$clang_tidy_major_version" >&2
    exit 2
    ;;
esac
case "$jobs" in
  *[!0-9]*|''|0*)
    printf 'clang-tidy: --jobs must be a positive integer, got: %s\n' "$jobs" >&2
    exit 2
    ;;
esac
if [ ! -d "$repository_root" ]; then
  printf 'clang-tidy: repository root does not exist: %s\n' "$repository_root" >&2
  exit 2
fi
if [ ! -e "${repository_root%/}/.git" ]; then
  printf "clang-tidy: skipped because '%s' is not a Git work tree.\n" "$repository_root"
  exit 0
fi

if ! git_command=$(command -v git 2>/dev/null); then
  printf '%s\n' 'clang-tidy: skipped because git is not available.'
  exit 0
fi
if ! inside_work_tree=$("$git_command" -C "$repository_root" rev-parse --is-inside-work-tree 2>/dev/null) ||
   [ "$inside_work_tree" != 'true' ]; then
  printf "clang-tidy: skipped because '%s' is not a Git work tree.\n" "$repository_root"
  exit 0
fi

if ! mkdir -p "$report_directory"; then
  printf 'clang-tidy: failed to create report directory: %s\n' "$report_directory" >&2
  exit 2
fi
temporary_directory=$(mktemp -d "${report_directory%/}/clang-tidy.XXXXXX") || {
  printf 'clang-tidy: failed to create a temporary directory under: %s\n' "$report_directory" >&2
  exit 2
}
staged_file_list="$temporary_directory/staged-files.txt"
unstaged_file_list="$temporary_directory/unstaged-files.txt"
unpushed_file_list="$temporary_directory/unpushed-files.txt"
changed_file_list="$temporary_directory/changed-files.txt"
analysis_file_list="$temporary_directory/analysis-files.txt"
report_file="$temporary_directory/report.txt"
git_error_file="$temporary_directory/git-error.txt"
compatible_configuration="$temporary_directory/.clang-tidy"
prepared_database_directory="$temporary_directory/compilation-database"
prepared_compilation_database="$prepared_database_directory/compile_commands.json"
manifest_file="$temporary_directory/manifest.tsv"
xargs_input="$temporary_directory/xargs-input"
worker_script="$temporary_directory/clang-tidy-worker.sh"
outputs_directory="$temporary_directory/outputs"

cleanup() {
  rm -f "$staged_file_list" "$unstaged_file_list" "$unpushed_file_list" "$changed_file_list" "$analysis_file_list" \
    "$report_file" "$git_error_file" "$compatible_configuration" "$manifest_file" "$xargs_input" \
    "$worker_script" "$prepared_compilation_database"
  if [ -d "$outputs_directory" ]; then
    rm -f "$outputs_directory"/*
    rmdir "$outputs_directory" 2>/dev/null || :
  fi
  rmdir "$prepared_database_directory" 2>/dev/null || :
  rmdir "$temporary_directory" 2>/dev/null || :
}
trap cleanup EXIT HUP INT TERM

if ! "$git_command" -c core.quotePath=false -C "$repository_root" diff --cached --name-only \
     --diff-filter=ACMRTUXB -- >"$staged_file_list" 2>"$git_error_file"; then
  cat "$git_error_file" >&2
  printf '%s\n' 'clang-tidy: failed to list staged files.' >&2
  exit 2
fi
if ! "$git_command" -c core.quotePath=false -C "$repository_root" diff --name-only --diff-filter=ACMRTUXB -- \
     >"$unstaged_file_list" 2>"$git_error_file"; then
  cat "$git_error_file" >&2
  printf '%s\n' 'clang-tidy: failed to list unstaged files.' >&2
  exit 2
fi
: >"$unpushed_file_list"
if "$git_command" -C "$repository_root" rev-parse --verify --quiet '@{upstream}^{commit}' >/dev/null 2>&1; then
  if ! "$git_command" -c core.quotePath=false -C "$repository_root" diff --name-only --diff-filter=ACMRTUXB \
       '@{upstream}...HEAD' -- >"$unpushed_file_list" 2>"$git_error_file"; then
    cat "$git_error_file" >&2
    printf '%s\n' 'clang-tidy: failed to list files changed in unpushed commits.' >&2
    exit 2
  fi
fi
if ! LC_ALL=C sort -u "$staged_file_list" "$unstaged_file_list" "$unpushed_file_list" >"$changed_file_list"; then
  printf '%s\n' 'clang-tidy: failed to merge the staged, unstaged, and unpushed file lists.' >&2
  exit 2
fi

changed_file_count=0
: >"$analysis_file_list"
while IFS= read -r relative_path || [ -n "$relative_path" ]; do
  case "$relative_path" in
    *.c|*.c++|*.cc|*.cpp|*.cu|*.cuh|*.cxx|*.h|*.h++|*.hh|*.hpp|*.hxx) ;;
    *) continue ;;
  esac

  changed_file="${repository_root%/}/$relative_path"
  [ -f "$changed_file" ] || continue
  printf '%s\n' "$relative_path" >>"$analysis_file_list"
  changed_file_count=$((changed_file_count + 1))
done <"$changed_file_list"

if [ "$changed_file_count" -eq 0 ]; then
  printf '%s\n' 'clang-tidy: no staged, unstaged, or unpushed C/C++ files to check.'
  exit 0
fi

compilation_database="${build_directory%/}/compile_commands.json"
if [ ! -f "$compilation_database" ]; then
  printf 'clang-tidy: compilation database does not exist: %s\n' "$compilation_database" >&2
  exit 2
fi
if [ ! -f "$clang_tidy_executable" ] && ! command -v "$clang_tidy_executable" >/dev/null 2>&1; then
  printf 'clang-tidy: executable is no longer available: %s\n' "$clang_tidy_executable" >&2
  exit 2
fi

analysis_build_directory=$build_directory
if [ "$prepare_msvc_compilation_database" -eq 1 ]; then
  if [ -z "$python_executable" ] || [ -z "$prepare_script" ]; then
    printf '%s\n' \
      'clang-tidy: --python-executable and --prepare-script are required with --prepare-msvc-compilation-database.' >&2
    exit 2
  fi
  if [ ! -f "$prepare_script" ]; then
    printf 'clang-tidy: compilation database preparation script does not exist: %s\n' "$prepare_script" >&2
    exit 2
  fi
  if [ ! -f "$python_executable" ] && ! command -v "$python_executable" >/dev/null 2>&1; then
    printf 'clang-tidy: Python executable is no longer available: %s\n' "$python_executable" >&2
    exit 2
  fi
  if ! mkdir -p "$prepared_database_directory" ||
     ! "$python_executable" "$prepare_script" --input "$compilation_database" \
       --output "$prepared_compilation_database" --remove-cmake-msvc-pch; then
    printf '%s\n' 'clang-tidy: failed to prepare the analysis-only compilation database.' >&2
    exit 2
  fi
  analysis_build_directory=$prepared_database_directory
fi

use_compatible_configuration=0
if [ "$clang_tidy_major_version" -lt 19 ] && [ -f "${repository_root%/}/.clang-tidy" ]; then
  if ! sed '/^ExcludeHeaderFilterRegex[[:space:]]*:/d' "${repository_root%/}/.clang-tidy" \
       >"$compatible_configuration"; then
    printf '%s\n' 'clang-tidy: failed to prepare a pre-19 compatible configuration.' >&2
    exit 2
  fi
  use_compatible_configuration=1
  printf '%s\n' \
    'clang-tidy: using a compatibility configuration without ExcludeHeaderFilterRegex, which requires clang-tidy 19.'
fi

issue_count=0
: >"$report_file"

# Translate the repository .clangd Diagnostics.ClangTidy rules into a per-file manifest so the
# analysis matches the editor behavior (for example clang-analyzer-* removal and third-party skips).
clangd_configuration="${repository_root%/}/.clangd"
manifest_ready=0
if [ -f "$clangd_configuration" ]; then
  clangd_rules_script=''
  if [ -n "$prepare_script" ]; then
    clangd_rules_candidate="${prepare_script%/*}/clang-tidy-clangd-rules.py"
    if [ -f "$clangd_rules_candidate" ]; then
      clangd_rules_script=$clangd_rules_candidate
    fi
  fi
  if [ -z "$clangd_rules_script" ]; then
    script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
    clangd_rules_candidate="$script_directory/clang-tidy-clangd-rules.py"
    if [ -f "$clangd_rules_candidate" ]; then
      clangd_rules_script=$clangd_rules_candidate
    fi
  fi
  if [ -n "$clangd_rules_script" ] && [ -n "$python_executable" ] &&
     { [ -f "$python_executable" ] || command -v "$python_executable" >/dev/null 2>&1; }; then
    if "$python_executable" "$clangd_rules_script" --clangd-config "$clangd_configuration" \
       --files "$analysis_file_list" --manifest "$manifest_file"; then
      manifest_ready=1
    else
      printf '%s\n' 'clang-tidy: warning: failed to evaluate .clangd rules; analyzing without them.' >&2
    fi
  else
    printf '%s\n' \
      'clang-tidy: warning: .clangd exists but no Python interpreter is available; .clangd rules are not applied.' \
      >&2
  fi
fi
if [ "$manifest_ready" -eq 0 ]; then
  : >"$manifest_file"
  while IFS= read -r relative_path || [ -n "$relative_path" ]; do
    [ -n "$relative_path" ] || continue
    case "$relative_path" in
      *"$(printf '\t')"*)
        # The TSV manifest cannot carry tab characters; match the pwsh twin: warn and skip the file
        printf 'clang-tidy: warning: paths with tab characters are not supported, skipped: %s\n' \
          "$relative_path" >&2
        continue
        ;;
    esac
    printf '%s\tanalyze\t\n' "$relative_path" >>"$manifest_file"
  done <"$analysis_file_list"
fi

# Each worker analyzes one file and stores the clang-tidy output and exit code under the outputs
# directory, so the main script can aggregate results in the deterministic manifest order.
mkdir -p "$outputs_directory"
cat >"$worker_script" <<'WORKER_EOF'
#!/bin/sh
# $1: "<index>\t<relative_path>\t<extra checks>"
worker_record=$1
file_index=$(printf '%s' "$worker_record" | cut -f1)
relative_path=$(printf '%s' "$worker_record" | cut -f2)
extra_checks=$(printf '%s' "$worker_record" | cut -f3)
if [ -z "$file_index" ] || [ -z "$relative_path" ]; then
  printf 'clang-tidy: malformed worker record: %s\n' "$worker_record" >&2
  exit 125
fi

changed_file="${worker_repository_root%/}/$relative_path"
line_filter_path=$(printf '%s' "$relative_path" | sed 's/\\/\\\\/g; s/"/\\"/g')
line_filter=$(printf '[{"name":"%s"}]' "$line_filter_path")

set -- --quiet --use-color=false "-p=$worker_analysis_build_directory" '--extra-arg=-Wno-error' \
  "--line-filter=$line_filter"
if [ "$worker_use_compatible_configuration" -eq 1 ]; then
  set -- "$@" "--config-file=$worker_compatible_configuration"
fi
if [ -n "$extra_checks" ]; then
  set -- "$@" "--checks=$extra_checks"
fi
worker_output_file="$worker_outputs_directory/$file_index.out"
if [ "$worker_prepare_msvc_compilation_database" -eq 1 ]; then
  set -- "$@" '--extra-arg=/WX-' '--extra-arg=-Wno-unused-command-line-argument'
  MSYS2_ARG_CONV_EXCL='--extra-arg=/WX-' "$worker_clang_tidy_executable" "$@" "$changed_file" \
    >"$worker_output_file" 2>&1
else
  "$worker_clang_tidy_executable" "$@" "$changed_file" >"$worker_output_file" 2>&1
fi
worker_exit_code=$?
printf '%s\n' "$worker_exit_code" >"$worker_outputs_directory/$file_index.code"
exit "$worker_exit_code"
WORKER_EOF

worker_repository_root=$repository_root
worker_analysis_build_directory=$analysis_build_directory
worker_use_compatible_configuration=$use_compatible_configuration
worker_compatible_configuration=$compatible_configuration
worker_prepare_msvc_compilation_database=$prepare_msvc_compilation_database
worker_clang_tidy_executable=$clang_tidy_executable
worker_outputs_directory=$outputs_directory
export worker_repository_root worker_analysis_build_directory worker_use_compatible_configuration
export worker_compatible_configuration worker_prepare_msvc_compilation_database
export worker_clang_tidy_executable worker_outputs_directory

: >"$xargs_input"
file_index=0
skipped_count=0
while IFS="$(printf '\t')" read -r relative_path action extra_checks || [ -n "$relative_path" ]; do
  [ -n "$relative_path" ] || continue
  if [ "$action" = 'skip' ]; then
    skipped_count=$((skipped_count + 1))
    printf '%s\n' 'clang-tidy: skipped because .clangd rules remove all checks for this file.' \
      >"$outputs_directory/$file_index.out"
    printf '%s\n' 0 >"$outputs_directory/$file_index.code"
  else
    printf '%s\t%s\t%s\0' "$file_index" "$relative_path" "$extra_checks" >>"$xargs_input"
  fi
  file_index=$((file_index + 1))
done <"$manifest_file"
manifest_file_count=$file_index

if [ -s "$xargs_input" ]; then
  xargs -0 -n 1 -P "$jobs" sh "$worker_script" <"$xargs_input"
  # Individual worker failures are reported from the per-file exit codes below; xargs exits 123
  # whenever any worker failed, which must not abort the aggregation.
fi

file_index=0
while IFS="$(printf '\t')" read -r relative_path action extra_checks || [ -n "$relative_path" ]; do
  [ -n "$relative_path" ] || continue
  changed_file="${repository_root%/}/$relative_path"
  current_report_file="$outputs_directory/$file_index.out"
  current_code_file="$outputs_directory/$file_index.code"
  if [ ! -f "$current_code_file" ]; then
    printf "clang-tidy: failed to analyze '%s' (missing worker result).\n" "$changed_file" >&2
    exit 2
  fi
  IFS= read -r clang_tidy_exit_code <"$current_code_file" || clang_tidy_exit_code=125
  case "$clang_tidy_exit_code" in
    ''|*[!0-9]*) clang_tidy_exit_code=125 ;;
  esac

  printf 'clang-tidy: %s\n' "$changed_file" >>"$report_file"
  cat "$current_report_file" >>"$report_file"
  if [ "$clang_tidy_exit_code" -ne 0 ]; then
    cat "$current_report_file" >&2
    printf "clang-tidy: failed to analyze '%s' with exit code %s.\n" "$changed_file" "$clang_tidy_exit_code" >&2
    exit 2
  fi

  current_issue_count=$(LC_ALL=C grep -E -c \
    '(:[0-9]+:[0-9]+:|\([0-9]+(,[0-9]+)?\):)[[:space:]]*(warning|error|fatal error):' \
    "$current_report_file") || current_issue_count=0
  issue_count=$((issue_count + current_issue_count))
  file_index=$((file_index + 1))
done <"$manifest_file"

if [ "$issue_count" -gt "$max_issues" ]; then
  printf 'clang-tidy report for %s staged, unstaged, or unpushed C/C++ file(s):\n' "$changed_file_count" >&2
  cat "$report_file" >&2
  printf 'clang-tidy: %s issue(s) exceed the configured maximum of %s.\n' "$issue_count" "$max_issues" >&2
  exit 1
fi

if [ "$skipped_count" -gt 0 ]; then
  printf '%s\n' \
    "clang-tidy: $issue_count issue(s) found in $((manifest_file_count - skipped_count)) analyzed C/C++ file(s)" \
    "($skipped_count skipped by .clangd rules, $changed_file_count changed in total);" \
    "maximum allowed is $max_issues."
else
  printf '%s\n' \
    "clang-tidy: $issue_count issue(s) found in $manifest_file_count staged, unstaged, or unpushed C/C++ file(s);" \
    "maximum allowed is $max_issues."
fi
