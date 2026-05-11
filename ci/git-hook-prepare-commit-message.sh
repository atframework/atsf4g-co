#!/usr/bin/env bash

COMMIT_MSG_FILE=${1:-}
COMMIT_SOURCE=${2:-}

if [[ -z "$COMMIT_MSG_FILE" ]] || [[ ! -e "$COMMIT_MSG_FILE" ]]; then
  exit 0
fi

is_truthy() {
  local value
  value=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
  case "$value" in
    "" | 0 | false | no | off)
      return 1
      ;;
    *)
      return 0
      ;;
  esac
}

has_existing_message() {
  awk '
    /^[[:space:]]*#/ { next }
    /^[[:space:]]*$/ { next }
    /^[[:space:]]*Add your commit message here[[:space:]]*$/ { next }
    { found = 1; exit }
    END { exit(found ? 0 : 1) }
  ' "$COMMIT_MSG_FILE"
}

strip_template_placeholder() {
  sed -e '/^[[:space:]]*Add your commit message here[[:space:]]*$/d' "$COMMIT_MSG_FILE"
}

content_hash() {
  if command -v sha1sum >/dev/null 2>&1; then
    sha1sum -b - | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 1 | awk '{print $1}'
  else
    cksum | awk '{print $1}'
  fi
}

remaining_ai_seconds() {
  local now elapsed remaining
  now=$(date +%s)
  elapsed=$((now - AI_START_TIME))
  remaining=$((AI_TIMEOUT_SECONDS - elapsed))
  if [[ $remaining -lt 1 ]]; then
    echo 0
  else
    echo "$remaining"
  fi
}

candidate_ai_seconds() {
  local remaining timeout
  remaining=$(remaining_ai_seconds)
  if [[ $remaining -lt 1 ]]; then
    echo 0
    return 0
  fi

  timeout=${AI_CANDIDATE_TIMEOUT_SECONDS:-$AI_TIMEOUT_SECONDS}
  if ! [[ "$timeout" =~ ^[0-9]+$ ]] || [[ $timeout -lt 1 ]]; then
    timeout=$AI_TIMEOUT_SECONDS
  fi

  if [[ $timeout -gt $remaining ]]; then
    timeout=$remaining
  fi

  echo "$timeout"
}

run_with_timeout() {
  local timeout_seconds=$1
  shift

  if [[ $timeout_seconds -lt 1 ]]; then
    return 124
  fi

  if command -v timeout >/dev/null 2>&1 && timeout --help 2>&1 | grep -qi 'coreutils'; then
    timeout "${timeout_seconds}s" "$@"
    return $?
  fi

  local output_file command_pid killer_pid result
  output_file=$(mktemp "${TMPDIR:-/tmp}/git-commit-ai.XXXXXX") || return 1
  "$@" >"$output_file" 2>/dev/null &
  command_pid=$!
  (
    sleep "$timeout_seconds"
    kill "$command_pid" >/dev/null 2>&1
  ) &
  killer_pid=$!

  wait "$command_pid" >/dev/null 2>&1
  result=$?
  kill "$killer_pid" >/dev/null 2>&1
  wait "$killer_pid" >/dev/null 2>&1
  cat "$output_file"
  rm -f "$output_file"
  return $result
}

decode_ai_output() {
  if ! command -v iconv >/dev/null 2>&1; then
    printf '%s\n' "$1"
    return 0
  fi

  if printf '%s\n' "$1" | iconv -f UTF-8 -t UTF-8 >/dev/null 2>&1; then
    printf '%s\n' "$1"
  else
    printf '%s\n' "$1" | iconv -f GB18030 -t UTF-8 2>/dev/null || printf '%s\n' "$1"
  fi
}

print_prompt_block() {
  local value=$1

  if [[ -n "$value" ]]; then
    printf '%s\n' "$value"
  else
    printf '(none)\n'
  fi
}

render_prompt_template() {
  local template_file=$1
  local line

  while IFS= read -r line || [[ -n "$line" ]]; do
    line=${line%$'\r'}
    case "$line" in
      '{{REPOSITORY_NAME}}')
        printf '%s\n' "$REPOSITORY_NAME"
        ;;
      '{{STATUS}}')
        print_prompt_block "$STATUS_CONTENT"
        ;;
      '{{FILES}}' | '{{FILE_LIST}}')
        print_prompt_block "$FILE_LIST"
        ;;
      '{{STAT}}' | '{{DIFF_STAT}}')
        print_prompt_block "$DIFF_STAT"
        ;;
      '{{DIFF}}' | '{{DIFF_CONTENT}}')
        print_prompt_block "$DIFF_CONTENT"
        ;;
      *)
        line=${line//'{{REPOSITORY_NAME}}'/$REPOSITORY_NAME}
        printf '%s\n' "$line"
        ;;
    esac
  done <"$template_file"
}

normalize_ai_message() {
  decode_ai_output "$1" | awk '
    BEGIN {
      started = 0
      printed = 0
      previous_blank = 0
      max_lines = 8
    }

    {
      gsub(/\r/, "")
      if ($0 ~ /^[[:space:]]*```/) next
      sub(/^[[:space:]]+/, "")
      sub(/[[:space:]]+$/, "")

      lowered = tolower($0)

      if (!started) {
        if ($0 == "") next
        if (lowered ~ /^(usage|error|failed|exception|warning|please login|not logged in|cannot find)([[:space:]:]|$)/) next
        if (lowered ~ /^(here is|here are|sure|certainly)([[:space:]:]|$)/) next

        if (lowered ~ /^git commit[[:space:]]/) {
          sub(/^git commit[[:space:]].*(-m|--message)[=[:space:]]*/, "")
          sub(/["'\''`“”][[:space:]].*$/, "")
        }

        sub(/^[Cc]ommit [Mm]essage[[:space:]]*[:：-][[:space:]]*/, "")
        sub(/^[Mm]essage[[:space:]]*[:：-][[:space:]]*/, "")
        sub(/^提交信息[[:space:]]*[:：-][[:space:]]*/, "")
        sub(/^建议提交信息[[:space:]]*[:：-][[:space:]]*/, "")
        sub(/^[*-][[:space:]]+/, "")
        sub(/^["'\''`“”]+/, "")
        sub(/["'\''`“”]+$/, "")
        sub(/[[:space:]]+$/, "")

        if (length($0) > 0 && $0 !~ /^#/) {
          print
          started = 1
          printed = 1
          previous_blank = 0
        }
        next
      }

      if (printed >= max_lines) next
      if ($0 == "") {
        if (!previous_blank) {
          print ""
          printed++
          previous_blank = 1
        }
        next
      }

      if ($0 !~ /^#/) {
        print
        printed++
        previous_blank = 0
      }
    }'
}

try_candidate_append_prompt() {
  local label=$1
  shift
  local executable=$1
  local candidate_timeout output result message

  command -v "$executable" >/dev/null 2>&1 || return 1
  candidate_timeout=$(candidate_ai_seconds)
  [[ $candidate_timeout -gt 0 ]] || return 1

  echo "Using $label to suggest commit message, please wait for a moment ..." 1>&2
  output=$(run_with_timeout "$candidate_timeout" "$@" "$AI_PROMPT" 2>/dev/null)
  result=$?
  [[ $result -eq 0 ]] || return 1

  message=$(normalize_ai_message "$output")
  if [[ -n "$message" ]]; then
    printf '%s\n' "$message"
    return 0
  fi

  return 1
}

try_candidate_stdin() {
  local label=$1
  shift
  local executable=$1
  local candidate_timeout output result message

  command -v "$executable" >/dev/null 2>&1 || return 1
  candidate_timeout=$(candidate_ai_seconds)
  [[ $candidate_timeout -gt 0 ]] || return 1

  echo "Using $label to suggest commit message, please wait for a moment ..." 1>&2
  output=$(printf '%s\n' "$AI_PROMPT" | run_with_timeout "$candidate_timeout" "$@" 2>/dev/null)
  result=$?
  [[ $result -eq 0 ]] || return 1

  message=$(normalize_ai_message "$output")
  if [[ -n "$message" ]]; then
    printf '%s\n' "$message"
    return 0
  fi

  return 1
}

suggest_commit_message() {
  # Prefer commonly used Chinese coding CLIs first, then other mainstream AI CLIs.
  try_candidate_append_prompt "Qwen Code CLI" qwen -p && return 0
  try_candidate_append_prompt "Qwen Code CLI" qwen --prompt && return 0
  try_candidate_append_prompt "Qwen Code CLI" qwen-code -p && return 0
  try_candidate_append_prompt "Qwen Code CLI" qwen-code --prompt && return 0
  try_candidate_append_prompt "Kimi CLI" kimi --quiet --afk -p && return 0
  try_candidate_append_prompt "Kimi CLI" kimi --print --afk -p && return 0
  try_candidate_append_prompt "DeepSeek CLI" deepseek -p && return 0
  try_candidate_append_prompt "DeepSeek CLI" deepseek --prompt && return 0
  try_candidate_append_prompt "DeepSeek CLI" deepseek && return 0
  try_candidate_append_prompt "DeepSeek CLI" deepseek-cli -p && return 0
  try_candidate_append_prompt "DeepSeek CLI" deepseek-cli --prompt && return 0
  try_candidate_append_prompt "DeepSeek TUI" deepseek-tui --prompt && return 0
  try_candidate_append_prompt "DeepSeek TUI" deepseek-tui -p && return 0
  try_candidate_stdin "DeepSeek TUI" deepseek-tui && return 0

  try_candidate_append_prompt "Kilo Code CLI" kilocode run && return 0
  try_candidate_append_prompt "Kilo Code CLI" kilo run && return 0
  try_candidate_append_prompt "Kilo Code CLI" kilo-code run && return 0
  try_candidate_append_prompt "Roo CLI" roo --prompt && return 0
  try_candidate_append_prompt "Roo CLI" roo -p && return 0
  try_candidate_append_prompt "Roo CLI" roo-code --prompt && return 0
  try_candidate_append_prompt "Roo CLI" roo-code -p && return 0
  try_candidate_append_prompt "OpenCode" opencode run && return 0
  try_candidate_append_prompt "OpenCode" opencode -p && return 0
  try_candidate_append_prompt "Claude Code CLI" claude -p && return 0
  try_candidate_append_prompt "Claude Code CLI" claude --print && return 0
  try_candidate_append_prompt "GitHub Copilot CLI" gh copilot -p && return 0
  try_candidate_append_prompt "GitHub Copilot CLI" gh copilot suggest -t git && return 0
  try_candidate_append_prompt "GitHub Copilot CLI" copilot suggest && return 0
  try_candidate_append_prompt "Gemini CLI" gemini -p && return 0
  try_candidate_append_prompt "OpenAI Codex CLI" codex exec && return 0

  return 1
}

# Ignore merge commit
if [[ "$COMMIT_SOURCE" == "merge" ]]; then
  exit 0
fi

if [[ -e "$COMMIT_MSG_FILE" ]]; then
  MERGE_COMMIT=1
  head -n 10 "$COMMIT_MSG_FILE" | grep -E '^[[:space:]]*Merge[[:space:]]' >/dev/null 2>&1 || MERGE_COMMIT=0
  if [[ $MERGE_COMMIT -ne 0 ]]; then
    exit
  fi
fi

# Ignore if already has message
if has_existing_message >/dev/null 2>&1; then
  exit 0
fi

# Allow users to opt out quickly: GIT_COMMIT_NO_MESSAGE_SUGGESTION=1 git commit
if is_truthy "${GIT_COMMIT_NO_MESSAGE_SUGGESTION:-}"; then
  exit 0
fi

GIT_ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
REPOSITORY_NAME=$(basename "$GIT_ROOT")
FILE_LIST=$(git diff --cached --name-status --submodule=short 2>/dev/null | head -c 1600)
DIFF_STAT=$(git diff --cached --stat --summary --submodule=short 2>/dev/null | head -c 1200)
STATUS_CONTENT=$(git status --short 2>/dev/null | head -c 800)

DIFF_CONTENT=""
if is_truthy "${GIT_COMMIT_MESSAGE_AI_INCLUDE_DIFF:-}"; then
  DIFF_CONTENT=$(git diff --cached --minimal --submodule=diff --no-ext-diff 2>/dev/null | head -c 2000)
fi

if [[ -z "$FILE_LIST" ]] && [[ -n "$DIFF_CONTENT" ]] && [[ ${#DIFF_CONTENT} -lt 64 ]]; then
  DIFF_CONTENT=$(git diff --minimal --submodule=diff --no-ext-diff 2>/dev/null | head -c 1200)
fi

if [[ -z "$FILE_LIST" ]] && [[ -z "$DIFF_CONTENT" ]] && [[ -z "$DIFF_STAT" ]] && [[ -z "$STATUS_CONTENT" ]]; then
  exit 0
fi

DIFF_SHA1=$(printf '%s\n%s\n%s\n%s\n' "$STATUS_CONTENT" "$FILE_LIST" "$DIFF_STAT" "$DIFF_CONTENT" | content_hash)
CACHE_SHA1_FILE="$COMMIT_MSG_FILE.sha1"
CACHE_MESSAGE_FILE="$COMMIT_MSG_FILE.ai-message"
SUGGESTION_COMMIT_MESSAGE=""

if [[ -s "$CACHE_MESSAGE_FILE" ]] && [[ -e "$CACHE_SHA1_FILE" ]] && [[ "$(cat "$CACHE_SHA1_FILE")" == "$DIFF_SHA1" ]]; then
  SUGGESTION_COMMIT_MESSAGE=$(normalize_ai_message "$(cat "$CACHE_MESSAGE_FILE")")
  if [[ -n "$SUGGESTION_COMMIT_MESSAGE" ]]; then
    echo "Using cached AI commit message, please wait for a moment ..." 1>&2
  fi
fi

if [[ -z "$SUGGESTION_COMMIT_MESSAGE" ]]; then
  AI_TIMEOUT_SECONDS=${GIT_COMMIT_MESSAGE_AI_TIMEOUT_SECONDS:-20}
  if ! [[ "$AI_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]]; then
    AI_TIMEOUT_SECONDS=20
  fi
  if [[ $AI_TIMEOUT_SECONDS -gt 20 ]]; then
    AI_TIMEOUT_SECONDS=20
  fi

  AI_CANDIDATE_TIMEOUT_SECONDS=${GIT_COMMIT_MESSAGE_AI_CANDIDATE_TIMEOUT_SECONDS:-$AI_TIMEOUT_SECONDS}
  if ! [[ "$AI_CANDIDATE_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] || [[ $AI_CANDIDATE_TIMEOUT_SECONDS -lt 1 ]]; then
    AI_CANDIDATE_TIMEOUT_SECONDS=$AI_TIMEOUT_SECONDS
  fi
  if [[ $AI_CANDIDATE_TIMEOUT_SECONDS -gt $AI_TIMEOUT_SECONDS ]]; then
    AI_CANDIDATE_TIMEOUT_SECONDS=$AI_TIMEOUT_SECONDS
  fi

  AI_PROMPT_TEMPLATE_FILE=${GIT_COMMIT_MESSAGE_AI_PROMPT_FILE:-"$GIT_ROOT/ci/git-commit-message.prompt.md"}
  if [[ ! -r "$AI_PROMPT_TEMPLATE_FILE" ]]; then
    exit 0
  fi

  AI_PROMPT=$(render_prompt_template "$AI_PROMPT_TEMPLATE_FILE")

  AI_START_TIME=$(date +%s)
  SUGGESTION_COMMIT_MESSAGE=$(suggest_commit_message)
  if [[ -n "$SUGGESTION_COMMIT_MESSAGE" ]]; then
    printf '%s\n' "$SUGGESTION_COMMIT_MESSAGE" >"$CACHE_MESSAGE_FILE"
    printf '%s\n' "$DIFF_SHA1" >"$CACHE_SHA1_FILE"
  fi
fi

if [[ -z "$SUGGESTION_COMMIT_MESSAGE" ]]; then
  exit 0
fi

{
  printf '%s\n\n' "$SUGGESTION_COMMIT_MESSAGE"
  if ! grep -F '# Issue:' "$COMMIT_MSG_FILE" >/dev/null 2>&1; then
    echo '# Issue: --bug=|--story=|--task=|--test=|--other=|Merge|merge'
  fi
  strip_template_placeholder
} >"$COMMIT_MSG_FILE.suggestion"
cp -f "$COMMIT_MSG_FILE.suggestion" "$COMMIT_MSG_FILE"
