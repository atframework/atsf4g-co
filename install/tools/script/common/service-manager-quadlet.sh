# Sourced by service-manager.sh for rootless Podman with systemd.
quadlet_quote() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//%/%%}"
  value="${value//$'\n'/\\n}"
  value="${value//$'\r'/\\r}"
  value="${value//$'\t'/\\t}"
  printf '"%s"' "$value"
}

quadlet_scalar() {
  [[ "$1" != *$'\n'* && "$1" != *$'\r'* && "$1" != *\\* ]] ||
    fail 'Quadlet scalar values cannot contain newlines or backslashes'
  printf '%s' "${1//%/%%}"
}

quadlet_setup() {
  local directory="${XDG_CONFIG_HOME:-$HOME/.config}/containers/systemd"
  local file="$directory/atsf4g-$KEY.container" option value setting
  case "${ATF_QUADLET_WAIT_NETWORK:-1}" in
    0|1) ;;
    *) fail 'ATF_QUADLET_WAIT_NETWORK must be 0 or 1' ;;
  esac
  mkdir -p "$directory"
  directory="$(cd "$directory" && pwd -P)"
  file="$directory/atsf4g-$KEY.container"
  if [[ -e "$file" || -L "$file" ]]; then
    [[ -L "$file" && "$(readlink "$file")" == "$STATE/unit.container" ]] ||
      fail 'Quadlet registration is owned by another installation'
  fi
  if [[ -e "$file.d" || -L "$file.d" ]]; then
    [[ -L "$file.d" && "$(readlink "$file.d")" == "$STATE/unit.container.d" ]] ||
      fail 'Quadlet drop-ins are owned by another installation'
  fi
  write_field quadlet-file "$file"
  write_field kind quadlet
  UNIT="atsf4g-$KEY.service"
  write_field unit "$UNIT"
  {
    if [[ "${ATF_QUADLET_WAIT_NETWORK:-1}" == 0 ]]; then
      printf '[Quadlet]\nDefaultDependencies=false\n'
    fi
    printf '[Unit]\nDescription=atsf4g container %s\nStartLimitIntervalSec=60\nStartLimitBurst=5\n' "$NAME"
    printf 'ConditionPathExists='; quadlet_scalar "$STATE/enabled"; printf '\n'
    printf '[Container]\nImage=%s\nContainerName=%s\n' "$CONTAINER_IMAGE" "$KEY"
    # Map common options to Quadlet keys; preserve remaining engine options explicitly.
    set -- "${CONTAINER_OPTIONS[@]}"
    while [[ $# -gt 0 ]]; do
      option="$1"; shift
      case "$option" in
        -p|--publish|-v|--volume|-e|--env|--env-file|--network|--user|--workdir|--entrypoint)
          [[ $# -gt 0 ]] || fail "missing value for $option"
          value="$1"; shift ;;
        --publish=*|--volume=*|--env=*|--env-file=*|--network=*|--user=*|--workdir=*|--entrypoint=*)
          value="${option#*=}"; option="${option%%=*}" ;;
        *) printf 'PodmanArgs='; quadlet_quote "$option"; printf '\n'; continue ;;
      esac
      case "$option" in
        -p|--publish) setting=PublishPort ;;
        -v|--volume) setting=Volume ;;
        -e|--env)
          setting=Environment
          if [[ "$value" != *=* ]]; then value="$value=${!value-}"; fi ;;
        --env-file) setting=EnvironmentFile ;;
        --network) setting=Network ;;
        --user) setting=User ;;
        --workdir) setting=WorkingDir ;;
        --entrypoint) setting=Entrypoint ;;
      esac
      printf '%s=' "$setting"
      if [[ "$setting" == Environment ]]; then quadlet_quote "$value"
      else quadlet_scalar "$value"; fi
      printf '\n'
    done
    if [[ ${#CONTAINER_COMMAND[@]} -gt 0 ]]; then
      printf 'Exec='
      for value in "${CONTAINER_COMMAND[@]}"; do quadlet_quote "$value"; printf ' '; done
      printf '\n'
    fi
    printf '[Service]\nRestart=on-failure\nRestartSec=2\nTimeoutStopSec=30\n'
    printf 'WorkingDirectory='; quadlet_scalar "$WORK_DIR"; printf '\n'
  } > "$STATE/unit.container.tmp"
  mv -f "$STATE/unit.container.tmp" "$STATE/unit.container"
  ln -sfn "$STATE/unit.container" "$file"
  mkdir -p "$STATE/unit.container.d"
  printf '[Install]\nWantedBy=default.target\n' > "$STATE/unit.container.d/10-atsf4g-enable.conf"
  ln -sfn "$STATE/unit.container.d" "$file.d"
  "$CONTROLLER" --user daemon-reload
  if [[ "$("$CONTROLLER" --user show -p LoadState --value "$UNIT")" != loaded ]]; then
    quadlet_disable
    quadlet_remove
    fail 'Podman Quadlet user generator did not produce the unit; install a Quadlet-capable Podman with cgroup v2'
  fi
}

quadlet_disable() {
  local file
  file="$(read_field quadlet-file)"
  rm -f "$file.d/10-atsf4g-enable.conf"
  "$CONTROLLER" --user daemon-reload
}

quadlet_remove() {
  local file
  file="$(read_field quadlet-file)"
  if [[ -L "$file" && "$(readlink "$file")" == "$STATE/unit.container" ]]; then
    rm -f "$file"
    if [[ -L "$file.d" && "$(readlink "$file.d")" == "$STATE/unit.container.d" ]]; then
      rm -f "$file.d"
    else
      # Older lifecycles created a real drop-in directory outside the package.
      rmdir "$file.d" 2>/dev/null || true
    fi
    "$CONTROLLER" --user daemon-reload
  fi
}
