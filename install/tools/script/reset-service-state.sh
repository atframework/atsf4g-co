#!/bin/bash
# Reset copied or corrupt runtime state. Stop live business services normally first.
set -euo pipefail
umask 077
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -L)"
source "$SCRIPT_DIR/common/service-manager-locks.sh"
source "$SCRIPT_DIR/common/service-runtime-paths.sh"
source "$SCRIPT_DIR/common/service-manager-state.sh"
fail() { echo "[reset-service-state] $*" >&2; exit 1; }
trap 'for directory in "${LOCK_DIRS[@]}"; do rmdir "$directory" 2>/dev/null || true; done' EXIT
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd -P)"
FORCE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--deploy-dir) [[ $# -ge 2 ]] || fail 'missing deploy directory'; ROOT="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    --dry-run) FORCE=0; shift ;;
    -h|--help)
      echo 'Usage: bash tools/script/reset-service-state.sh [-d DEPLOY_DIR] [--force|--dry-run]'
      echo 'Default: preview. --force backs up and clears stopped runtime state; live services are refused.'
      exit 0 ;;
    *) fail "unknown option: $1" ;;
  esac
done
ROOT="$(cd "$ROOT" && pwd -P)"
[[ "$ROOT" != / && -f "$ROOT/tools/script/service-manager.sh" ]] || fail 'not a deployment directory'
[[ -d /proc ]] || fail 'live-process checks require Linux /proc'
for directory in "$ROOT/var/service-manager" "$ROOT/var/service-manager/control" "$ROOT/var/service-manager/reset-backups"; do
  [[ "$(realpath -m "$directory")" == "$ROOT/"* ]] || fail "runtime directory points outside deployment: $directory"
done
lock_dir "$ROOT/var/service-manager/control" 7
systemd_begin

PATHS=()
while IFS= read -r -d '' path; do
  [[ "$path" == "$ROOT/"* ]] || fail "runtime path escapes deployment: $path"
  [[ "$(realpath -m "$(dirname "$path")")" == "$ROOT/"* ]] || fail "runtime parent points outside deployment: $path"
  duplicate=0
  for existing in "${PATHS[@]}"; do
    if [[ "$path" == "$existing" || "$path" == "$existing/"* ]]; then duplicate=1; break; fi
  done
  if [[ $duplicate -eq 0 ]]; then PATHS+=("$path"); fi
done < <(service_runtime_paths "$ROOT")

# Use actual registration targets, never absolute paths or controllers from copied metadata.
LINKS=(); UNITS=()
for directory in "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user" \
    "${XDG_CONFIG_HOME:-$HOME/.config}/containers/systemd" "$HOME/.local/state/atsf4g/instances"; do
  [[ -d "$directory" ]] || continue
  while IFS= read -r -d '' link; do
    target="$(realpath -m -- "$link")"
    [[ "$target" == "$ROOT/"* ]] || continue
    name="$(basename "$link")"
    [[ "$name" != 'atsf4g@.service' ]] || continue
    LINKS+=("$link")
    case "$directory:$name" in
      */instances:*) unit="atsf4g@$name.service" ;;
      *:*.container.d) unit="${name%.container.d}.service" ;;
      *:*.container) unit="${name%.container}.service" ;;
      *:*.service) unit="$name" ;;
      *) continue ;;
    esac
    [[ "$unit" =~ ^atsf4g[-@a-zA-Z0-9_.]+\.service$ ]] || fail "invalid owned unit name: $unit"
    UNITS+=("$unit")
  done < <(find "$directory" -type l -print0)
done
for unit in "${UNITS[@]}"; do
  status="$(systemctl --user show --property=ActiveState,MainPID,Job "$unit")" || fail 'systemd user manager is unavailable'
  active=''; pid=''; job=''
  while IFS='=' read -r key value; do
    case "$key" in ActiveState) active="$value" ;; MainPID) pid="$value" ;; Job) job="$value" ;; esac
  done <<< "$status"
  [[ "$pid" == 0 && -z "$job" ]] || fail "unit is still running or queued: $unit; run its normal stop script first"
  case "$active" in inactive|failed) ;; *) fail "unit is not stopped: $unit ($active)" ;; esac
done

# PID numbers copied from another host are insufficient evidence of ownership.
for path in "${PATHS[@]}"; do
  [[ -d "$path" && ! -L "$path" ]] || continue
  if read_checked_record "$path/process.record" && [[ ${#RECORD_VALUES[@]} -eq 3 && "${RECORD_VALUES[0]}" == "$path" ]]; then
    pid="${RECORD_VALUES[1]}"; recorded_identity="${RECORD_VALUES[2]}"
  elif [[ -s "$path/pid" && -s "$path/identity" ]]; then
    pid="$(cat "$path/pid")"; recorded_identity="$(cat "$path/identity")"
  else continue; fi
  [[ "$pid" =~ ^[1-9][0-9]*$ && -d "/proc/$pid" ]] || continue
  identity="$(ps -p "$pid" -o lstart= 2>/dev/null || true)"
  cwd="$(readlink "/proc/$pid/cwd" 2>/dev/null || true)"
  if [[ "$identity" == "$recorded_identity" && ( "$cwd" == "$ROOT" || "$cwd" == "$ROOT/"* ) ]]; then
    fail "business process $pid is still running; run its normal stop script first"
  fi
done
for process in /proc/[0-9]*; do
  [[ "${process##*/}" != "$$" ]] || continue
  executable="$(readlink "$process/exe" 2>/dev/null || true)"
  [[ "$executable" != "$ROOT/"* ]] || fail "executable is still running: ${process##*/}; stop it first"
  while IFS= read -r -d '' argument; do
    case "$argument" in
      "$ROOT"/*/bin/*|"$ROOT"/bin/*)
        fail "business process ${process##*/} still uses this deployment; run its normal stop script first" ;;
    esac
  done < "$process/cmdline" 2>/dev/null || true
done

# Refuse a running container even if its attached client has disappeared.
for path in "${PATHS[@]}"; do
  [[ -s "$path/container-engine" && -s "$path/key" ]] || continue
  engine="$(basename "$(cat "$path/container-engine")")"; name="$(cat "$path/key")"
  case "$engine" in docker|podman|nerdctl) ;; *) continue ;; esac
  [[ "$name" =~ ^[a-zA-Z0-9_.-]+$ ]] || continue
  command -v "$engine" >/dev/null || continue
  if [[ "$("$engine" inspect -f '{{.State.Running}}' "$name" 2>/dev/null || true)" == true ]]; then
    mounts="$("$engine" inspect -f '{{range .Mounts}}{{println .Source}}{{end}}' "$name")"
    digest="$(printf '%s' "$ROOT" | state_digest)"
    if [[ "$name" == *-"${digest:0:16}" || "$mounts" == "$ROOT"* || "$mounts" == *$'\n'"$ROOT"* ]]; then
      fail "container $name is still running; stop it before clearing state"
    fi
  fi
done

SUPERVISOR_CONF="$ROOT/var/service-manager/supervisor/supervisord.conf"
SUPERVISOR_PID=0
if [[ -s "${SUPERVISOR_CONF%/*}/supervisord.pid" ]]; then
  pid="$(cat "${SUPERVISOR_CONF%/*}/supervisord.pid")"
  if [[ "$pid" =~ ^[1-9][0-9]*$ && -r "/proc/$pid/cmdline" ]]; then
    while IFS= read -r -d '' argument; do
      if [[ "$argument" == /* && "$(realpath -m -- "$argument")" == "$SUPERVISOR_CONF" ]]; then SUPERVISOR_PID="$pid"; fi
    done < "/proc/$pid/cmdline"
  fi
fi
if [[ "$SUPERVISOR_PID" != 0 ]]; then
  command -v supervisorctl >/dev/null || fail 'activate the Supervisor environment before resetting its private daemon'
  [[ "$(supervisorctl -c "$SUPERVISOR_CONF" pid)" == "$SUPERVISOR_PID" ]] || fail 'Supervisor socket belongs to another daemon'
  status="$(supervisorctl -c "$SUPERVISOR_CONF" status 2>&1 || true)"
  [[ "$status" != *RUNNING* && "$status" != *STARTING* && "$status" != *BACKOFF* && "$status" != *STOPPING* && "$status" != *EXITED* ]] ||
    fail 'private Supervisor is not fully stopped; run its programs normal stop scripts first'
fi

printf '[reset-service-state] Deployment: %s\n' "$ROOT"
for path in "${PATHS[@]}" "${LINKS[@]}"; do printf '  %s\n' "$path"; done
if [[ $FORCE -eq 0 ]]; then echo '[reset-service-state] Preview only. Add --force to back up and clear these paths.'; exit; fi
if [[ ${#PATHS[@]} -eq 0 && ${#LINKS[@]} -eq 0 ]]; then echo '[reset-service-state] Nothing to clear.'; exit; fi
if [[ "$SUPERVISOR_PID" != 0 ]]; then supervisorctl -c "$SUPERVISOR_CONF" shutdown; fi
BACKUP="$ROOT/var/service-manager/reset-backups/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir -p "$BACKUP"
index=0
for link in "${LINKS[@]}"; do
  ((index+=1)); mkdir -p "$BACKUP/registrations"
  readlink "$link" > "$BACKUP/registrations/$index.target"
  printf '%s\n' "$link" > "$BACKUP/registrations/$index.path"
  rm -f -- "$link"
done
for path in "${PATHS[@]}"; do
  [[ -e "$path" || -L "$path" ]] || continue
  destination="$BACKUP/${path#"$ROOT"/}"
  mkdir -p "$(dirname "$destination")"
  mv -- "$path" "$destination"
done
if [[ ${#LINKS[@]} -gt 0 ]]; then
  systemctl --user daemon-reload
  for unit in "${UNITS[@]}"; do
    if [[ "$(systemctl --user show -p ActiveState --value "$unit")" == failed ]]; then systemctl --user reset-failed "$unit"; fi
  done
fi
if [[ -n "${ATF_SUPERVISOR_DIR:-}" && "$(realpath -m "$ATF_SUPERVISOR_DIR")" != "$ROOT/"* ]]; then
  echo '[reset-service-state] External ATF_SUPERVISOR_DIR was retained; clear its deployment-specific configuration separately.'
fi
printf '[reset-service-state] Cleared. Backup: %s\n' "$BACKUP"
