#!/bin/sh
# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

repository_root=''
python_venv_dir=''
max_issues=''
report_directory=''

print_usage() {
  printf '%s\n' \
    'Usage: cpplint.sh --repository-root <path> --python-venv-dir <path> --max-issues <count> --report-directory <path>'
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --repository-root)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      repository_root=$2
      shift 2
      ;;
    --python-venv-dir)
      [ "$#" -ge 2 ] || { print_usage >&2; exit 2; }
      python_venv_dir=$2
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
    *)
      printf 'cpplint: unknown argument: %s\n' "$1" >&2
      print_usage >&2
      exit 2
      ;;
  esac
done

if [ -z "$repository_root" ] || [ -z "$max_issues" ] || [ -z "$report_directory" ]; then
  print_usage >&2
  exit 2
fi
case "$max_issues" in
  *[!0-9]*|'')
    printf 'cpplint: --max-issues must be a non-negative integer, got: %s\n' "$max_issues" >&2
    exit 2
    ;;
esac
if [ ! -d "$repository_root" ]; then
  printf 'cpplint: repository root does not exist: %s\n' "$repository_root" >&2
  exit 2
fi
if [ ! -e "${repository_root%/}/.git" ]; then
  printf "cpplint: skipped because '%s' is not a Git work tree.\n" "$repository_root"
  exit 0
fi

if ! git_command=$(command -v git 2>/dev/null); then
  printf '%s\n' 'cpplint: skipped because git is not available.'
  exit 0
fi
if ! inside_work_tree=$("$git_command" -C "$repository_root" rev-parse --is-inside-work-tree 2>/dev/null) ||
   [ "$inside_work_tree" != 'true' ]; then
  printf "cpplint: skipped because '%s' is not a Git work tree.\n" "$repository_root"
  exit 0
fi

if ! mkdir -p "$report_directory"; then
  printf 'cpplint: failed to create report directory: %s\n' "$report_directory" >&2
  exit 2
fi
temporary_directory=$(mktemp -d "${report_directory%/}/cpplint.XXXXXX") || {
  printf 'cpplint: failed to create a temporary directory under: %s\n' "$report_directory" >&2
  exit 2
}
staged_file_list="$temporary_directory/staged-files.txt"
unstaged_file_list="$temporary_directory/unstaged-files.txt"
changed_file_list="$temporary_directory/changed-files.txt"
report_file="$temporary_directory/report.txt"
current_report_file="$temporary_directory/current-report.txt"
git_error_file="$temporary_directory/git-error.txt"

cleanup() {
  rm -f "$staged_file_list" "$unstaged_file_list" "$changed_file_list" "$report_file" "$current_report_file" \
    "$git_error_file"
  rmdir "$temporary_directory" 2>/dev/null || :
}
trap cleanup EXIT HUP INT TERM

if ! "$git_command" -c core.quotePath=false -C "$repository_root" diff --cached --name-only \
     --diff-filter=ACMRTUXB -- >"$staged_file_list" 2>"$git_error_file"; then
  cat "$git_error_file" >&2
  printf '%s\n' 'cpplint: failed to list staged files.' >&2
  exit 2
fi
if ! "$git_command" -c core.quotePath=false -C "$repository_root" diff --name-only --diff-filter=ACMRTUXB -- \
     >"$unstaged_file_list" 2>"$git_error_file"; then
  cat "$git_error_file" >&2
  printf '%s\n' 'cpplint: failed to list unstaged files.' >&2
  exit 2
fi
if ! LC_ALL=C sort -u "$staged_file_list" "$unstaged_file_list" >"$changed_file_list"; then
  printf '%s\n' 'cpplint: failed to merge the staged and unstaged file lists.' >&2
  exit 2
fi

if [ -n "$python_venv_dir" ]; then
  venv_search_dir=$python_venv_dir
  case "$venv_search_dir" in
    [A-Za-z]:/*)
      if command -v cygpath >/dev/null 2>&1; then
        venv_search_dir=$(cygpath -u "$venv_search_dir")
      fi
      ;;
  esac
  VIRTUAL_ENV=$python_venv_dir
  PATH="$venv_search_dir/bin:$venv_search_dir/Scripts:$PATH"
  export VIRTUAL_ENV PATH
fi

changed_file_count=0
issue_count=0
: >"$report_file"
while IFS= read -r relative_path || [ -n "$relative_path" ]; do
  case "$relative_path" in
    *.c|*.c++|*.cc|*.cpp|*.cu|*.cuh|*.cxx|*.h|*.h++|*.hh|*.hpp|*.hxx) ;;
    *) continue ;;
  esac

  changed_file="${repository_root%/}/$relative_path"
  [ -f "$changed_file" ] || continue
  changed_file_count=$((changed_file_count + 1))

  if ! command -v cpplint >/dev/null 2>&1; then
    printf '%s\n' 'cpplint: executable not found in the project Python virtual environment or inherited PATH.' >&2
    exit 2
  fi

  cpplint "--repository=$repository_root" "$changed_file" >"$current_report_file" 2>&1
  cpplint_exit_code=$?
  cat "$current_report_file" >>"$report_file"
  current_issue_count=$(sed -n 's/^Total errors found:[[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/\1/p' \
    "$current_report_file" | tail -n 1)
  if [ -z "$current_issue_count" ]; then
    if [ "$cpplint_exit_code" -ne 0 ]; then
      cat "$current_report_file" >&2
      printf "cpplint: failed to analyze '%s' with exit code %s.\n" "$changed_file" "$cpplint_exit_code" >&2
      exit 2
    fi
    current_issue_count=0
  fi
  issue_count=$((issue_count + current_issue_count))
done <"$changed_file_list"

if [ "$changed_file_count" -eq 0 ]; then
  printf '%s\n' 'cpplint: no staged or unstaged C/C++ files to check.'
  exit 0
fi

if [ "$issue_count" -gt "$max_issues" ]; then
  printf 'cpplint report for %s staged or unstaged C/C++ file(s):\n' "$changed_file_count" >&2
  cat "$report_file" >&2
  printf 'cpplint: %s issue(s) exceed the configured maximum of %s.\n' "$issue_count" "$max_issues" >&2
  exit 1
fi

printf 'cpplint: %s issue(s) found in %s staged or unstaged C/C++ file(s); maximum allowed is %s.\n' \
  "$issue_count" "$changed_file_count" "$max_issues"
