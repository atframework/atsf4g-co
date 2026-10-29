# Sourced by the controller and process wrappers. Records contain data, never shell code.
state_digest() {
  local digest
  if command -v sha256sum >/dev/null; then digest="$(sha256sum)"
  else digest="$(shasum -a 256)"; fi
  printf '%s' "${digest%% *}"
}

write_checked_record() {
  local path="$1" digest; shift
  printf '%s\0' "$@" > "$path.payload.tmp"
  digest="$(state_digest < "$path.payload.tmp")"
  { printf 'atsf4g-state-v1 %s\n' "$digest"; cat "$path.payload.tmp"; } > "$path.tmp"
  mv -f "$path.tmp" "$path"
  rm -f "$path.payload.tmp"
}

read_checked_record() {
  local path="$1" header digest value
  RECORD_VALUES=()
  [[ -f "$path" ]] || return 1
  IFS= read -r header < "$path" || return 1
  [[ "$header" =~ ^atsf4g-state-v1\ [0-9a-f]{64}$ ]] || return 1
  digest="$(tail -n +2 "$path" | state_digest)"
  [[ "$header" == "atsf4g-state-v1 $digest" ]] || return 1
  while IFS= read -r -d '' value; do RECORD_VALUES+=("$value"); done < <(tail -n +2 "$path")
}

STATE_FIELDS=(backend key controller kind unit supervisor-dir stop-script unit-link quadlet-file working-directory)
save_state_record() {
  local field values=("$STATE" "$1")
  PHASE="$1"
  for field in "${STATE_FIELDS[@]}"; do
    if [[ -f "$STATE/$field" ]]; then values+=("$(read_field "$field")")
    else values+=(''); fi
  done
  write_checked_record "$STATE/lifecycle.record" "${values[@]}"
  cp "$STATE/lifecycle.record" "$STATE/lifecycle.record.bak.tmp"
  mv -f "$STATE/lifecycle.record.bak.tmp" "$STATE/lifecycle.record.bak"
}

restore_state_record() {
  local path index field restored=0
  for path in "$STATE/lifecycle.record" "$STATE/lifecycle.record.bak"; do
    if read_checked_record "$path" && [[ ${#RECORD_VALUES[@]} -eq 12 && "${RECORD_VALUES[0]}" == "$STATE" ]]; then
      case "${RECORD_VALUES[1]}" in running|stopping|killing|stopped) restored=1; break ;; esac
    fi
  done
  if [[ $restored -eq 0 ]]; then
    [[ ! -e "$STATE/lifecycle.record" && ! -e "$STATE/lifecycle.record.bak" ]] ||
      fail 'both lifecycle records are corrupt or belong to another instance; state retained'
    return 1
  fi
  PHASE="${RECORD_VALUES[1]}"
  for ((index=0; index<${#STATE_FIELDS[@]}; index++)); do
    field="${STATE_FIELDS[index]}"
    if [[ -n "${RECORD_VALUES[index+2]}" ]]; then
      if [[ ! -f "$STATE/$field" || "$(read_field "$field")" != "${RECORD_VALUES[index+2]}" ]]; then
        write_field "$field" "${RECORD_VALUES[index+2]}"
      fi
    elif [[ -f "$STATE/$field" && -s "$STATE/$field" || "$field" == controller ]]; then
      write_field "$field" ''
    fi
  done
  # Repair either redundant copy only after its checksum and ownership were verified.
  save_state_record "$PHASE"
  case "$PHASE" in
    running) rm -f "$STATE/stopped" "$STATE/force-kill"; touch "$STATE/enabled" ;;
    stopping) rm -f "$STATE/stopped" "$STATE/enabled" "$STATE/force-kill" ;;
    killing) rm -f "$STATE/stopped" "$STATE/enabled"; touch "$STATE/force-kill" ;;
    stopped) rm -f "$STATE/enabled"; write_field stopped yes ;;
  esac
}

has_state_record() {
  [[ -e "$STATE/backend" || -e "$STATE/unit" || -e "$STATE/lifecycle.record" || -e "$STATE/lifecycle.record.bak" ]]
}

record_process_identity() {
  local directory="$1" pid="$2" identity
  identity="$(ps -p "$pid" -o lstart=)"
  write_checked_record "$directory/process.record" "$directory" "$pid" "$identity"
  printf '%s\n' "$pid" > "$directory/pid"
  printf '%s\n' "$identity" > "$directory/identity"
}

valid_process_record() {
  read_checked_record "$STATE/process.record" &&
    [[ ${#RECORD_VALUES[@]} -eq 3 && "${RECORD_VALUES[0]}" == "$STATE" &&
       "${RECORD_VALUES[1]}" =~ ^[1-9][0-9]*$ && -n "${RECORD_VALUES[2]}" ]]
}

restore_process_identity() {
  local pid identity
  if ! valid_process_record; then return 0; fi
  pid="${RECORD_VALUES[1]}"; identity="${RECORD_VALUES[2]}"
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 0
  [[ "$(ps -p "$pid" -o lstart= 2>/dev/null)" == "$identity" ]] || return 0
  [[ "$(ps -p "$pid" -o stat=)" != *Z* ]] || return 0
  write_field pid "$pid"
  write_field identity "$identity"
  # Generated atapp instances also expose the application's PID file to their stop script.
  if [[ "$STATE" == *.pid.service && -f "$STATE/ready.record" ]] &&
      cmp -s "$STATE/process.record" "$STATE/ready.record" &&
      [[ ! -f "${STATE%.service}" || "$(cat "${STATE%.service}")" != "$pid" ]]; then
    printf '%s\n' "$pid" > "${STATE%.service}.tmp"
    mv -f "${STATE%.service}.tmp" "${STATE%.service}"
  fi
}
