#!/bin/bash
# Shared lifecycle controller. State belongs to an instance, not to a backend.
set -euo pipefail
umask 077

# Keep the published path when tools/ is linked to the source tree by a local build.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PACKAGE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/common/service-manager-quadlet.sh"
fail() { echo "[service-manager] $*" >&2; exit 1; }

select_backend() {
  local requested="${1:-${ATF_SERVICE_MANAGER:-auto}}"
  case "$requested" in
    auto)
      if command -v systemctl >/dev/null && systemctl --user show-environment >/dev/null 2>&1; then
        echo systemd
      elif command -v supervisord >/dev/null && command -v supervisorctl >/dev/null; then
        echo supervisord
      else
        echo none
      fi ;;
    systemd)
      command -v systemctl >/dev/null && systemctl --user show-environment >/dev/null 2>&1 ||
        fail 'systemd user manager is unavailable'
      echo systemd ;;
    supervisord)
      command -v supervisord >/dev/null && command -v supervisorctl >/dev/null ||
        fail 'supervisord and supervisorctl must be installed'
      echo supervisord ;;
    none) echo none ;;
    *) fail "unknown backend: $requested (auto|systemd|supervisord|none)" ;;
  esac
}

# Lock descriptors are closed in every launched process. macOS can use the mkdir fallback.
lock_dir() {
  local directory="$1" fd="$2"
  mkdir -p "$directory"
  if command -v flock >/dev/null; then
    if [[ "$fd" == 9 ]]; then exec 9>"$directory/lock"; else exec 8>"$directory/lock"; fi
    flock -x -w 30 "$fd" || fail "lock timeout: $directory"
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
LOCK_DIRS=()
ROLLBACK_START=0
on_exit() {
  local result=$?
  trap - EXIT
  set +e
  if [[ $ROLLBACK_START -eq 1 ]]; then
    (set -e; stop_backend stop)
    if [[ $? -ne 0 ]]; then
      echo '[service-manager] startup rollback failed; retry stop using the recorded state' >&2
    fi
  fi
  for directory in "${LOCK_DIRS[@]}"; do rmdir "$directory"; done
  exit "$result"
}
trap on_exit EXIT

write_field() { printf '%s\n' "$2" > "$STATE/$1.tmp"; mv -f "$STATE/$1.tmp" "$STATE/$1"; }
read_field() { cat "$STATE/$1"; }
live_pid() {
  local pid identity
  if [[ -f "$STATE/kind" && "$(read_field kind)" == quadlet ]]; then
    pid="$("$(read_field controller)" --user show -p MainPID --value "$(read_field unit)")" || return 1
    [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
    printf '%s\n' "$pid"
    return
  fi
  [[ -s "$STATE/pid" && -s "$STATE/identity" ]] || return 1
  read -r pid < "$STATE/pid"
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  identity="$(ps -p "$pid" -o lstart= 2>/dev/null)" || return 1
  [[ -n "$identity" && "$identity" == "$(cat "$STATE/identity")" ]] || return 1
  # A detached child can remain a zombie until its parent reaps it.
  [[ "$(ps -p "$pid" -o stat=)" != *Z* ]] || return 1
  printf '%s\n' "$pid"
}

supervisor_ctl() { "$CONTROLLER" -c "$SUPERVISOR_DIR/supervisord.conf" "$@"; }
supervisor_setup() {
  SUPERVISOR_DIR="${ATF_SUPERVISOR_DIR:-$PACKAGE_DIR/var/service-manager/supervisor}"
  mkdir -p "$SUPERVISOR_DIR"
  SUPERVISOR_DIR="$(cd "$SUPERVISOR_DIR" && pwd -P)"
  [[ "$SUPERVISOR_DIR" != *$'\n'* && "$SUPERVISOR_DIR" != *$'\r'* && "$SUPERVISOR_DIR" != *[\\\;]* ]] ||
    fail 'ATF_SUPERVISOR_DIR cannot contain newlines, backslashes or semicolons'
  lock_dir "$SUPERVISOR_DIR" 8
  mkdir -p "$SUPERVISOR_DIR/programs"
  CONTROLLER="$(command -v supervisorctl)"
  write_field controller "$CONTROLLER"
  write_field supervisor-dir "$SUPERVISOR_DIR"
  # Escape absolute paths directly: Supervisor expands %(here)s before percent interpolation.
  local ini_directory="${SUPERVISOR_DIR//%/%%}"
  if [[ ! -f "$SUPERVISOR_DIR/supervisord.conf" ]]; then
    local socket="$SUPERVISOR_DIR/control.sock" socket_hash
    if [[ ${#socket} -ge 100 ]]; then
      socket_hash="$(printf '%s' "$SUPERVISOR_DIR" | cksum)"
      mkdir -p "${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}/atsf4g-$UID"
      socket="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}/atsf4g-$UID/supervisor-${socket_hash%% *}.sock"
    fi
    socket="${socket//%/%%}"
    cat > "$SUPERVISOR_DIR/supervisord.conf" <<EOF
; atsf4g private supervisor; never use a system-wide daemon
[unix_http_server]
file=$socket
chmod=0700
[supervisord]
logfile=$ini_directory/supervisord.log
logfile_maxbytes=10MB
pidfile=$ini_directory/supervisord.pid
childlogdir=$ini_directory
[rpcinterface:supervisor]
supervisor.rpcinterface_factory=supervisor.rpcinterface:make_main_rpcinterface
[supervisorctl]
serverurl=unix://$socket
[include]
files=programs/*.ini
EOF
  fi
  if ! supervisor_ctl pid >/dev/null 2>&1; then
    "$(command -v supervisord)" -c "$SUPERVISOR_DIR/supervisord.conf" 8>&- 9>&-
    supervisor_ctl pid >/dev/null || fail 'private supervisord did not start'
  fi
}

load_state() {
  BACKEND="$(read_field backend)"
  KEY="$(read_field key)"
  CONTROLLER="$(read_field controller)"
  KIND="$(read_field kind)"
  UNIT="$(read_field unit)"
  if [[ "$BACKEND" == supervisord ]]; then SUPERVISOR_DIR="$(read_field supervisor-dir)"; fi
}

reset_stopped_unit_failure() {
  # systemd keeps failed units loaded even after their files and links are removed.
  if [[ "$BACKEND" == systemd && "$("$CONTROLLER" --user show -p ActiveState --value "$UNIT")" == failed ]]; then
    "$CONTROLLER" --user reset-failed "$UNIT"
  fi
}

register_stop_script() {
  local script="$1" directory="$2"
  write_field stop-script "$script"
  {
    echo '#!/bin/bash'
    printf 'exec /bin/bash %q %q "$@"\n' "$SCRIPT_DIR/common/service-manager-stop.sh" "$STATE"
  } > "$STATE/stop.sh"
  {
    echo '#!/bin/bash'
    printf 'if [[ -f %q ]]; then source %q; fi\n' "$STATE/runtime-environment.sh" "$STATE/runtime-environment.sh"
    printf 'cd %q || exit 1\n' "$directory"
    printf 'exec /bin/bash %q --atapp-stop-command "$@"\n' "$script"
  } > "$STATE/stop-command.sh"
}

start_backend() {
  case "$BACKEND" in
    systemd)
      "$CONTROLLER" --user reset-failed "$UNIT" >/dev/null 2>&1 || true
      if [[ "$KIND" == quadlet ]]; then
        "$CONTROLLER" --user start "$UNIT"
      else
        "$CONTROLLER" --user enable --now "$UNIT"
      fi ;;
    supervisord)
      supervisor_ctl reread
      supervisor_ctl update "$KEY"
      local status attempts=0
      while :; do
        status="$(supervisor_ctl status "$KEY" 2>&1 || true)"
        case "$status" in
          *RUNNING*) break ;;
          *STOPPED*) supervisor_ctl start "$KEY"; break ;;
          *FATAL*|*EXITED*|*'no such process'*) fail "$status" ;;
        esac
        ((attempts+=1))
        [[ $attempts -lt 150 ]] || fail "supervisor startup timed out: $status"
        sleep 0.1
      done ;;
    none)
      if ! live_pid >/dev/null; then
        nohup /bin/bash "$STATE/run.sh" >> "$STATE/output.log" 2>&1 < /dev/null 8>&- 9>&- &
        local child=$! attempts=0
        until live_pid >/dev/null; do
          kill -0 "$child" 2>/dev/null || fail "process exited; see $STATE/output.log"
          ((attempts+=1))
          [[ $attempts -lt 100 ]] || fail 'process did not write its identity'
          sleep 0.05
        done
      fi ;;
    *) fail "invalid recorded backend: $BACKEND" ;;
  esac
  local ready=0
  until live_pid >/dev/null; do
    ((ready+=1))
    [[ $ready -lt 100 ]] || fail "process did not start: $KEY"
    sleep 0.05
  done
}

stop_backend() {
  local mode="$1" stop_job='' pid='' status=''
  shift
  # Persist intent first. A restarted daemon must not launch this instance again.
  rm -f "$STATE/enabled"
  if [[ "$mode" == kill ]]; then touch "$STATE/force-kill"
  else
    rm -f "$STATE/force-kill"
    { printf 'set --'; if [[ $# -gt 0 ]]; then printf ' %q' "$@"; fi; printf '\n'; } > "$STATE/stop-arguments.sh"
  fi
  case "$BACKEND" in
    systemd)
      if [[ "$KIND" == quadlet ]]; then quadlet_disable
      else "$CONTROLLER" --user disable "$UNIT"; fi
      if [[ "$mode" == stop && -s "$STATE/stop-script" ]]; then /bin/bash "$STATE/stop.sh"; fi
      "$CONTROLLER" --user stop "$UNIT" & stop_job=$!
      if [[ "$mode" == kill ]]; then
        # Queue the stop before SIGKILL so Restart=on-failure cannot race the kill.
        while kill -0 "$stop_job" 2>/dev/null; do
          status="$("$CONTROLLER" --user show -p ActiveState --value "$UNIT")"
          [[ "$status" == deactivating || "$status" == inactive || "$status" == failed ]] && break
          sleep 0.05
        done
      fi ;;
    supervisord)
      supervisor_ctl pid >/dev/null || fail 'recorded supervisord is unavailable; state retained'
      rm -f "$SUPERVISOR_DIR/programs/$KEY.ini"
      if [[ "$mode" == stop && -s "$STATE/stop-script" ]]; then /bin/bash "$STATE/stop.sh"; fi
      status="$(supervisor_ctl status "$KEY" 2>&1 || true)"
      case "$status" in
        *RUNNING*|*STARTING*|*BACKOFF*|*STOPPING*)
          supervisor_ctl stop "$KEY" & stop_job=$!
          if [[ "$mode" == kill ]]; then
            while kill -0 "$stop_job" 2>/dev/null; do
              status="$(supervisor_ctl status "$KEY" 2>&1 || true)"
              [[ "$status" == *STOPPING* ]] && break
              [[ "$status" == *STOPPED* || "$status" == *EXITED* || "$status" == *FATAL* ]] && break
              sleep 0.05
            done
          fi ;;
      esac ;;
    none)
      if [[ "$mode" == stop && -s "$STATE/stop-script" ]]; then /bin/bash "$STATE/stop.sh"
      elif pid="$(live_pid)"; then
        if [[ "$mode" != kill ]]; then kill -TERM "$pid"; fi
      fi ;;
    *) fail "invalid recorded backend: $BACKEND" ;;
  esac
  # Killing a foreground container client alone does not stop the container.
  if [[ -s "$STATE/container-engine" ]]; then
    local engine
    engine="$(read_field container-engine)"
    source "$STATE/container-environment.sh"
    "$engine" info >/dev/null || fail 'recorded container engine is unavailable; state retained'
    if "$engine" container inspect "$KEY" >/dev/null 2>&1; then
      if [[ "$mode" == kill ]]; then "$engine" kill "$KEY" >/dev/null || true
      elif ! "$engine" stop -t 25 "$KEY" >/dev/null; then
        # Quadlet or the attached client may already have finished removing it.
        if [[ "$("$engine" inspect -f '{{.State.Running}}' "$KEY" 2>/dev/null || true)" == true ]]; then
          fail "container stop failed: $KEY"
        fi
      fi
      # Let conmon/containerd finish recording the exit before killing the client group.
      local container_wait=0
      while [[ "$("$engine" inspect -f '{{.State.Running}}' "$KEY" 2>/dev/null || true)" == true ]]; do
        ((container_wait+=1))
        [[ $container_wait -lt 300 ]] || fail "container did not stop: $KEY"
        sleep 0.1
      done
    fi
  fi
  if [[ "$mode" == kill ]]; then
    case "$BACKEND" in
      systemd) "$CONTROLLER" --user kill --signal=KILL "$UNIT" 2>/dev/null || true ;;
      supervisord)
        if [[ -s "$STATE/stop-script" ]] && pid="$(live_pid)"; then kill -KILL "$pid"; fi
        if [[ "$(supervisor_ctl status "$KEY" 2>&1 || true)" == *STOPPING* ]]; then
          supervisor_ctl signal KILL "$KEY" || true
        fi ;;
      none) if pid="$(live_pid)"; then kill -KILL "$pid"; fi ;;
    esac
  fi
  if [[ -n "$stop_job" ]]; then wait "$stop_job"; fi
  local attempts=0
  while pid="$(live_pid)"; do
    ((attempts+=1))
    if [[ $attempts -eq 300 ]]; then
      if [[ "$mode" == stop && -s "$STATE/stop-script" ]]; then fail "business stop command did not stop process: $pid"
      else kill -KILL "$pid"; fi
    fi
    [[ $attempts -lt 350 ]] || fail "process did not stop: $pid"
    sleep 0.1
  done
  if [[ "$BACKEND" == supervisord ]]; then
    supervisor_ctl reread
    # A failed startup may never have registered a group with the daemon.
    if [[ "$(supervisor_ctl status "$KEY" 2>&1 || true)" != *'no such process'* ]]; then
      supervisor_ctl update "$KEY"
    fi
  fi
  [[ ! -f "$STATE/stop-command.error" || "$mode" == kill ]] || fail 'business stop command failed; state retained'
  if [[ -s "$STATE/container-engine" ]]; then
    if "$engine" container inspect "$KEY" >/dev/null 2>&1; then
      [[ "$("$engine" inspect -f '{{.State.Running}}' "$KEY")" == false ]] || fail "container still running: $KEY"
      "$engine" rm "$KEY" >/dev/null
    fi
  fi
  if [[ "$KIND" == quadlet ]]; then quadlet_remove; fi
  if [[ -f "$STATE/unit-link" ]]; then
    local unit_link
    unit_link="$(read_field unit-link)"
    if [[ -L "$unit_link" && "$(readlink "$unit_link")" == "$STATE/$UNIT" ]]; then
      rm -f "$unit_link"
      "$CONTROLLER" --user daemon-reload
    fi
  fi
  # Older lifecycles may still have a recorded shared-template registry link.
  if [[ -f "$STATE/registry-link" ]]; then
    local registry_link
    registry_link="$(read_field registry-link)"
    if [[ -L "$registry_link" && "$(readlink "$registry_link")" == "$STATE" ]]; then rm -f "$registry_link"; fi
  fi
  reset_stopped_unit_failure
  write_field stopped yes
}

ACTION="${1:-help}"
shift || true
if [[ "$ACTION" == select ]]; then select_backend "${1:-${ATF_SERVICE_MANAGER:-auto}}"; exit; fi
if [[ "$ACTION" == help || $# -eq 0 ]]; then
  echo 'Usage: service-manager.sh select [auto|systemd|supervisord|none]'
  echo '       service-manager.sh start STATE NAME CWD [--stop-script SCRIPT] -- COMMAND [ARG...]'
  echo '       service-manager.sh start STATE NAME CWD --container ENGINE IMAGE -- [RUN_OPTIONS] [-- COMMAND ARG...]'
  echo '       service-manager.sh stop STATE [--stop-script SCRIPT --] [ARG...]'
  echo '       service-manager.sh kill|pid|status STATE'
  exit 0
fi
STATE="$1"; shift
if [[ "$ACTION" != start && ! -f "$STATE/backend" ]]; then
  [[ "$ACTION" == stop || "$ACTION" == kill ]] && exit 0
  exit 1
fi
mkdir -p "$STATE"
STATE="$(cd "$STATE" && pwd -P)"
if [[ "$ACTION" == pid ]]; then live_pid; exit; fi
lock_dir "$STATE" 9

case "$ACTION" in
  start)
    [[ $# -ge 4 ]] || fail 'start requires NAME CWD -- COMMAND'
    NAME="$1"; WORK_DIR="$2"; shift 2
    [[ "$NAME" =~ ^[a-zA-Z0-9_.-]+$ && ${#NAME} -lt 180 ]] || fail 'invalid or overly long service name'
    WORK_DIR="$(cd "$WORK_DIR" && pwd -P)"
    ENGINE=''
    STOP_SCRIPT=''
    if [[ "$1" == --stop-script ]]; then
      [[ -f "$2" ]] || fail "stop script not found: $2"
      STOP_SCRIPT="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
      shift 2
    fi
    CONTAINER_IMAGE=''
    if [[ "$1" == --container ]]; then
      ENGINE="$(command -v "$2")"
      CONTAINER_IMAGE="$3"
      [[ "$CONTAINER_IMAGE" != -* && "$CONTAINER_IMAGE" != *[[:space:]%]* ]] || fail 'invalid container image'
      shift 3
    fi
    [[ "$1" == -- ]] || fail 'expected -- COMMAND [ARG...]'
    shift
    [[ -n "$ENGINE" || $# -gt 0 ]] || fail 'missing command'
    if [[ -z "$ENGINE" ]]; then
      (cd "$WORK_DIR" && command -v -- "$1" >/dev/null) || fail "executable not found: $1"
    fi
    REQUESTED="${ATF_SERVICE_MANAGER:-auto}"
    if [[ -f "$STATE/backend" && ! -f "$STATE/stopped" ]]; then
      load_state
      [[ "$REQUESTED" == auto || "$REQUESTED" == "$BACKEND" ]] || fail 'stop the instance before changing its backend'
      [[ -f "$STATE/enabled" ]] || fail 'previous stop is incomplete; retry stop before starting'
      if [[ "$BACKEND" == supervisord ]]; then lock_dir "$SUPERVISOR_DIR" 8; fi
      # Keep the original command, environment and controller for an existing lifecycle.
      start_backend
      exit 0
    fi
    BACKEND="$(select_backend "$REQUESTED")"
    if command -v sha256sum >/dev/null; then HASH="$(printf '%s' "$STATE" | sha256sum)"
    else HASH="$(printf '%s' "$STATE" | shasum -a 256)"; fi
    KEY="$NAME-${HASH:0:16}"
    write_field key "$KEY"
    KIND=process
    UNIT="atsf4g@$KEY.service"
    write_field kind "$KIND"
    write_field unit "$UNIT"
    write_field backend "$BACKEND"
    write_field stopped yes
    write_field controller ''
    rm -f "$STATE/stop-script" "$STATE/command.sh" "$STATE/stop-command.done" "$STATE/stop-command.error" "$STATE/force-kill" "$STATE/stop-arguments.sh"
    {
      echo '#!/bin/bash'
      printf 'exec /bin/bash %q %q "$@"\n' "$SCRIPT_DIR/common/service-manager-stop.sh" "$STATE"
    } > "$STATE/stop.sh"
    if [[ -n "$STOP_SCRIPT" ]]; then
      register_stop_script "$STOP_SCRIPT" "$WORK_DIR"
    fi
    rm -f "$STATE/container-engine"
    if [[ -n "$ENGINE" ]]; then
      CONTAINER_OPTIONS=()
      CONTAINER_COMMAND=()
      while [[ $# -gt 0 && "$1" != -- ]]; do CONTAINER_OPTIONS+=("$1"); shift; done
      if [[ $# -gt 0 ]]; then shift; CONTAINER_COMMAND=("$@"); fi
      for argument in "${CONTAINER_OPTIONS[@]}"; do
        case "$argument" in --detach*|-d|-dit|-itd|--restart*|--rm*|--name*|--sdnotify*|--replace*|--cidfile*|--cgroups*) fail "container lifecycle option is owned by service-manager: $argument";; esac
      done
      write_field container-engine "$ENGINE"
      {
        for variable in HOME XDG_CONFIG_HOME XDG_DATA_HOME XDG_RUNTIME_DIR DOCKER_HOST DOCKER_CONTEXT DOCKER_CONFIG \
            CONTAINER_HOST CONTAINER_CONNECTION CONTAINERS_CONF CONTAINERS_STORAGE_CONF; do
          printf 'unset %s\n' "$variable"
          if printenv "$variable" >/dev/null; then printf 'export %s=%q\n' "$variable" "${!variable}"; fi
        done
      } > "$STATE/container-environment.sh"
      set -- "$ENGINE" run --rm --restart=no --name "$KEY" "${CONTAINER_OPTIONS[@]}" "$CONTAINER_IMAGE" "${CONTAINER_COMMAND[@]}"
    fi
    {
      # Preserve deployment inputs across user-manager and daemon environment boundaries.
      while IFS= read -r variable; do
        case "$variable" in
          PATH|LD_LIBRARY_PATH|DYLD_LIBRARY_PATH|ATAPP_*|ATBUS_*|PROJECT_*|ETCD_*|REDIS_*|ASAN_OPTIONS|LSAN_OPTIONS|TSAN_OPTIONS|UBSAN_OPTIONS|MALLOC_CONF|XDG_RUNTIME_DIR|DBUS_SESSION_BUS_ADDRESS|CONTAINER_HOST|DOCKER_HOST|DOCKER_CONTEXT|DOCKER_CONFIG)
            printf 'export %s=%q\n' "$variable" "${!variable}" ;;
        esac
      done < <(compgen -e)
    } > "$STATE/runtime-environment.sh"
    {
      echo '#!/bin/bash'
      printf '[[ -f %q ]] || exit 0\n' "$STATE/enabled"
      printf 'rm -f %q %q %q\n' "$STATE/stop-command.done" "$STATE/stop-command.error" "$STATE/stop-arguments.sh"
      printf 'echo $$ > %q\n' "$STATE/pid"
      printf 'ps -p $$ -o lstart= > %q\n' "$STATE/identity"
      printf 'cd %q || exit 1\n' "$WORK_DIR"
      printf 'source %q\n' "$STATE/runtime-environment.sh"
      if [[ -n "$ENGINE" ]]; then printf 'source %q\n' "$STATE/container-environment.sh"; fi
      printf 'exec'; printf ' %q' "$@"; printf '\n'
    } > "$STATE/run.sh.tmp"
    mv -f "$STATE/run.sh.tmp" "$STATE/run.sh"
    STOP_AS_GROUP=true
    if [[ "$BACKEND" == supervisord && -n "$STOP_SCRIPT" ]]; then
      STOP_AS_GROUP=false
      mv -f "$STATE/run.sh" "$STATE/command.sh"
      { echo '#!/bin/bash'; printf 'exec /bin/bash %q %q\n' "$SCRIPT_DIR/common/service-manager-worker.sh" "$STATE"; } > "$STATE/run.sh"
    fi
    case "$BACKEND" in
      systemd)
        CONTROLLER="$(command -v systemctl)"
        write_field controller "$CONTROLLER"
        if [[ -n "$ENGINE" && "$(basename "$ENGINE")" == podman ]]; then
          KIND=quadlet
          quadlet_setup
        else
          UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
          mkdir -p "$UNIT_DIR"
          UNIT_FILE="$STATE/$UNIT"
          UNIT_LINK="$UNIT_DIR/$UNIT"
          if [[ -e "$UNIT_LINK" || -L "$UNIT_LINK" ]]; then
            [[ -L "$UNIT_LINK" && "$(readlink "$UNIT_LINK")" == "$UNIT_FILE" ]] ||
              fail 'unit registration is owned by another installation'
          fi
          if ! cmp -s "$SCRIPT_DIR/common/atsf4g@.service" "$UNIT_FILE"; then
            cp "$SCRIPT_DIR/common/atsf4g@.service" "$UNIT_FILE.tmp"
            chmod 600 "$UNIT_FILE.tmp"
            mv -f "$UNIT_FILE.tmp" "$UNIT_FILE"
          fi
          ln -sfn "$UNIT_FILE" "$UNIT_LINK"
          write_field unit-link "$UNIT_LINK"
          "$CONTROLLER" --user daemon-reload
        fi ;;
      supervisord)
        supervisor_setup
        # % is escaped for ConfigParser; shlex handles the double-quoted command path.
        RUN_PATH="${STATE//%/%%}/run.sh"
        [[ "$RUN_PATH" != *[\"\\\;]* ]] || fail 'supervisor state path cannot contain quotes, backslash or semicolon'
        # Supervisor expands program logfile paths twice, unlike command paths.
        LOG_PATH="${SUPERVISOR_DIR//%/%%%%}/$KEY.log"
        cat > "$SUPERVISOR_DIR/programs/$KEY.ini" <<EOF
[program:$KEY]
command=/bin/bash "$RUN_PATH"
autostart=true
autorestart=unexpected
exitcodes=0
startsecs=1
startretries=3
stopsignal=TERM
stopwaitsecs=30
stopasgroup=$STOP_AS_GROUP
killasgroup=true
redirect_stderr=true
stdout_logfile=$LOG_PATH
stdout_logfile_maxbytes=10MB
stdout_logfile_backups=2
EOF
        ;;
    esac
    rm -f "$STATE/stopped"
    ROLLBACK_START=1
    touch "$STATE/enabled"
    start_backend
    ROLLBACK_START=0
    ;;
  stop|kill)
    load_state
    if [[ "$ACTION" == stop && "${1:-}" == --stop-script ]]; then
      [[ -f "$2" ]] || fail "stop script not found: $2"
      STOP_SCRIPT="$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")"
      if [[ -s "$STATE/stop-script" ]]; then
        [[ "$(read_field stop-script)" == "$STOP_SCRIPT" ]] || fail 'stop script differs from the recorded instance'
      else
        # Upgrade a lifecycle started before business stop callbacks were recorded.
        register_stop_script "$STOP_SCRIPT" "$(dirname "$STOP_SCRIPT")"
      fi
      shift 2
      [[ "${1:-}" == -- ]] || fail 'expected -- before stop command arguments'
      shift
    fi
    if [[ "$BACKEND" == supervisord ]]; then lock_dir "$SUPERVISOR_DIR" 8; fi
    if [[ ! -f "$STATE/stopped" ]]; then stop_backend "$ACTION" "$@"
    else reset_stopped_unit_failure; fi ;;
  status)
    load_state
    echo "backend=$BACKEND name=$KEY"
    live_pid ;;
  *) fail "unknown action: $ACTION" ;;
esac
