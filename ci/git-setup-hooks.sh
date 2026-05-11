#!/usr/bin/env bash

if ! git_dir="$(git rev-parse --git-dir 2>/dev/null)" || ! git rev-parse --show-toplevel >/dev/null 2>&1; then
  echo "not found git repo: $PWD" 1>&2
  exit 1
fi

normalize_shell_path() {
  local path=$1

  if command -v cygpath >/dev/null 2>&1 && [[ "$path" =~ ^[A-Za-z]:[\\/] ]]; then
    cygpath -u "$path" 2>/dev/null || printf '%s\n' "$path"
  else
    printf '%s\n' "$path"
  fi
}

absolute_path() {
  local path=$1

  case "$path" in
    /* | [A-Za-z]:/* | [A-Za-z]:\\*)
      normalize_shell_path "$path"
      ;;
    *)
      normalize_shell_path "$PWD/$path"
      ;;
  esac
}

git_dir=$(absolute_path "$git_dir")
hooks_dir="$git_dir/hooks"

if ! mkdir -p "$hooks_dir"; then
  echo "create git hooks directory failed: $hooks_dir" 1>&2
  exit 1
fi

ensure_hook_file() {
  local hook_file=$1

  if [[ ! -f "$hook_file" ]]; then
    if ! printf '#!/usr/bin/env bash\n' >"$hook_file"; then
      echo "create git hook failed: $hook_file" 1>&2
      exit 1
    fi
  fi

  if ! chmod +x "$hook_file"; then
    echo "make git hook executable failed: $hook_file" 1>&2
    exit 1
  fi
}

append_hook_runner() {
  local hook_file=$1
  local script_name=$2
  local setup_script_name

  setup_script_name=${0##*/}
  ensure_hook_file "$hook_file"

  if grep -F -- "ci/$script_name" "$hook_file" >/dev/null 2>&1; then
    return 0
  fi

  if ! cat >>"$hook_file" <<EOF

# Add by $setup_script_name: ci/$script_name
if repo_root="\$(git rev-parse --show-toplevel 2>/dev/null)" && [ -f "\$repo_root/ci/$script_name" ]; then
  if [ -n "\${BASH:-}" ]; then
    "\$BASH" "\$repo_root/ci/$script_name" "\$@" || exit \$?
  else
    bash "\$repo_root/ci/$script_name" "\$@" || exit \$?
  fi
fi
EOF
  then
    echo "append git hook runner failed: $hook_file" 1>&2
    exit 1
  fi
}

append_hook_runner "$hooks_dir/post-checkout" "git-hook-post-checkout.sh"
append_hook_runner "$hooks_dir/post-merge" "git-hook-post-merge.sh"
append_hook_runner "$hooks_dir/pre-commit" "git-hook-pre-commit.sh"
append_hook_runner "$hooks_dir/post-commit" "git-hook-post-commit.sh"
append_hook_runner "$hooks_dir/pre-push" "git-hook-pre-push.sh"
append_hook_runner "$hooks_dir/prepare-commit-msg" "git-hook-prepare-commit-message.sh"

git config --local --replace-all push.recurseSubmodules on-demand
