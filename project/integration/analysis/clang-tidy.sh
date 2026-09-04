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

print_usage() {
  printf '%s\n' \
    'Usage: clang-tidy.sh --repository-root <path> --build-directory <path> --clang-tidy-executable <path>' \
    '       --max-issues <count> --clang-tidy-major-version <version> --report-directory <path>' \
    '       [--python-executable <path> --prepare-script <path> --prepare-msvc-compilation-database]'
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
current_report_file="$temporary_directory/current-report.txt"
git_error_file="$temporary_directory/git-error.txt"
compatible_configuration="$temporary_directory/.clang-tidy"
prepared_database_directory="$temporary_directory/compilation-database"
prepared_compilation_database="$prepared_database_directory/compile_commands.json"

cleanup() {
  rm -f "$staged_file_list" "$unstaged_file_list" "$unpushed_file_list" "$changed_file_list" "$analysis_file_list" \
    "$report_file" "$current_report_file" "$git_error_file" "$compatible_configuration" \
    "$prepared_compilation_database"
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
while IFS= read -r changed_file || [ -n "$changed_file" ]; do
  relative_path=$changed_file
  changed_file="${repository_root%/}/$relative_path"
  line_filter_path=$(printf '%s' "$relative_path" | sed 's/\\/\\\\/g; s/"/\\"/g')
  line_filter=$(printf '[{"name":"%s"}]' "$line_filter_path")

  set -- --quiet --use-color=false "-p=$analysis_build_directory" '--extra-arg=-Wno-error' \
    "--line-filter=$line_filter"
  if [ "$use_compatible_configuration" -eq 1 ]; then
    set -- "$@" "--config-file=$compatible_configuration"
  fi
  if [ "$prepare_msvc_compilation_database" -eq 1 ]; then
    set -- "$@" '--extra-arg=/WX-' '--extra-arg=-Wno-unused-command-line-argument'
    MSYS2_ARG_CONV_EXCL='--extra-arg=/WX-' "$clang_tidy_executable" "$@" "$changed_file" \
      >"$current_report_file" 2>&1
  else
    "$clang_tidy_executable" "$@" "$changed_file" >"$current_report_file" 2>&1
  fi
  clang_tidy_exit_code=$?

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
done <"$analysis_file_list"

if [ "$issue_count" -gt "$max_issues" ]; then
  printf 'clang-tidy report for %s staged, unstaged, or unpushed C/C++ file(s):\n' "$changed_file_count" >&2
  cat "$report_file" >&2
  printf 'clang-tidy: %s issue(s) exceed the configured maximum of %s.\n' "$issue_count" "$max_issues" >&2
  exit 1
fi

printf 'clang-tidy: %s issue(s) found in %s staged, unstaged, or unpushed C/C++ file(s); maximum allowed is %s.\n' \
  "$issue_count" "$changed_file_count" "$max_issues"
