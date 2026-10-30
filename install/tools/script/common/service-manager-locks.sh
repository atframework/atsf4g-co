# Lock order: deployment (7), instance (9), user systemd or private Supervisor (8).
LOCK_DIRS=()
lock_dir() {
  local directory="$1" fd="$2" mode="${3:--x}"
  mkdir -p "$directory"
  if command -v flock >/dev/null; then
    case "$fd" in
      7) exec 7>"$directory/lock" ;;
      8) exec 8>"$directory/lock" ;;
      9) exec 9>"$directory/lock" ;;
    esac
    flock "$mode" -w 30 "$fd" || fail "lock timeout: $directory"
  else
    local attempts=0
    until mkdir "$directory/lock.d" 2>/dev/null; do
      ((attempts+=1))
      [[ $attempts -lt 300 ]] || fail "lock timeout: $directory/lock.d; check its owner before removing it"
      sleep 0.1
    done
    LOCK_DIRS+=("$directory/lock.d")
  fi
}

unlock_dir() {
  local directory="$1" fd="$2" index
  if command -v flock >/dev/null; then flock -u "$fd"
  else
    rmdir "$directory/lock.d"
    for index in "${!LOCK_DIRS[@]}"; do
      if [[ "${LOCK_DIRS[index]}" == "$directory/lock.d" ]]; then unset 'LOCK_DIRS[index]'; fi
    done
  fi
  case "$fd" in 7) exec 7>&- ;; 8) exec 8>&- ;; 9) exec 9>&- ;; esac
}

systemd_begin() {
  [[ "${SYSTEMD_LOCKED:-0}" == 0 ]] || return 0
  SYSTEMD_LOCK_DIR="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}/atsf4g-$UID/systemd"
  lock_dir "$SYSTEMD_LOCK_DIR" 8
  SYSTEMD_LOCKED=1
}

systemd_end() {
  [[ "${SYSTEMD_LOCKED:-0}" == 1 ]] || return 0
  unlock_dir "$SYSTEMD_LOCK_DIR" 8
  SYSTEMD_LOCKED=0
}
