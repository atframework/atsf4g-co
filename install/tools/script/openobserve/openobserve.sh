#!/bin/sh
# Copyright 2026 atframework
# Manage a single-node OpenObserve OSS container for local development.
# The data directory, generated password, and telemetry values survive start/stop.
# Usage: openobserve.sh <download|start|stop|kill|cleanup|status> [options]

set -eu

SCRIPT_DIR="$(CDPATH='' cd "$(dirname -- "$0")" && pwd)"
LOCAL_TEST_ENV_ROOT="$(CDPATH='' cd "$SCRIPT_DIR/.." && pwd)"
if [ -d "$LOCAL_TEST_ENV_ROOT/../../cloud-native" ]; then
  ATF_SERVICE_MANAGER_ROOT="$(CDPATH='' cd "$LOCAL_TEST_ENV_ROOT/../.." && pwd)"
else
  ATF_SERVICE_MANAGER_ROOT="$LOCAL_TEST_ENV_ROOT"
fi
export ATF_SERVICE_MANAGER_ROOT
WORK_DIR="$LOCAL_TEST_ENV_ROOT/var/local-test-env/openobserve"
HTTP_PORT=5080
GRPC_PORT=5081
OPENOBSERVE_IMAGE="public.ecr.aws/zinclabs/openobserve:v1.1.2"
CONTAINER_ENGINE=""
CONTAINER_MEMORY="3g"
CONTAINER_CPUS="2"
ORGANIZATION="atsf4g"
ROOT_USER_EMAIL="admin@owent.net"
RETENTION_DAYS=14
STARTUP_TIMEOUT=120
SERVICE_MANAGER="$SCRIPT_DIR/../service-manager.sh"
INITIALIZER="$SCRIPT_DIR/init_openobserve.py"

COMMAND="${1:-}"
if [ $# -gt 0 ]; then
  shift
fi
while [ $# -gt 0 ]; do
  case "$1" in
    --service-manager)
      ATF_SERVICE_MANAGER="$2"
      export ATF_SERVICE_MANAGER
      shift 2
      ;;
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --http-port)
      HTTP_PORT="$2"
      shift 2
      ;;
    --grpc-port)
      GRPC_PORT="$2"
      shift 2
      ;;
    --image)
      OPENOBSERVE_IMAGE="$2"
      shift 2
      ;;
    --container-engine)
      CONTAINER_ENGINE="$2"
      shift 2
      ;;
    --memory)
      CONTAINER_MEMORY="$2"
      shift 2
      ;;
    --cpus)
      CONTAINER_CPUS="$2"
      shift 2
      ;;
    --organization)
      ORGANIZATION="$2"
      shift 2
      ;;
    --root-user-email)
      ROOT_USER_EMAIL="$2"
      shift 2
      ;;
    --retention-days)
      RETENTION_DAYS="$2"
      shift 2
      ;;
    --startup-timeout)
      STARTUP_TIMEOUT="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

case "$RETENTION_DAYS" in
  ''|*[!0-9]*)
    echo "Error: --retention-days must be an integer of at least 3." >&2
    exit 1
    ;;
esac
if [ "$RETENTION_DAYS" -lt 3 ]; then
  echo "Error: --retention-days must be at least 3." >&2
  exit 1
fi

DATA_DIR="$WORK_DIR/data"
PASSWORD_FILE="$WORK_DIR/openobserve-root-password.txt"
OTEL_VALUES_FILE="$WORK_DIR/values/modules/telemetry.yaml"
CONTAINER_NAME="atsf4g-openobserve-local"
CID_FILE="$WORK_DIR/openobserve.cid"
SERVICE_STATE="$WORK_DIR/service"
has_service_state() {
  [ -f "$SERVICE_STATE/backend" ] || [ -f "$SERVICE_STATE/unit" ] ||
    [ -f "$SERVICE_STATE/lifecycle.record" ] || [ -f "$SERVICE_STATE/lifecycle.record.bak" ]
}
if has_service_state && [ "$COMMAND" != download ]; then
  bash "$SERVICE_MANAGER" recover "$SERVICE_STATE"
fi
if [ -f "$SERVICE_STATE/container-engine" ]; then
  if [ "$COMMAND" != start ] && [ "$COMMAND" != download ]; then
    CONTAINER_ENGINE=$(cat "$SERVICE_STATE/container-engine")
  fi
  CONTAINER_NAME=$(cat "$SERVICE_STATE/key")
fi

find_engine() {
  if [ -n "$CONTAINER_ENGINE" ]; then
    if command -v "$CONTAINER_ENGINE" >/dev/null 2>&1; then
      echo "$CONTAINER_ENGINE"
      return 0
    fi
    return 1
  fi
  for engine in docker podman nerdctl; do
    if command -v "$engine" >/dev/null 2>&1; then
      echo "$engine"
      return 0
    fi
  done
  return 1
}

require_engine() {
  local engine
  if ! engine="$(find_engine)"; then
    if [ -n "$CONTAINER_ENGINE" ]; then
      echo "Error: container engine '$CONTAINER_ENGINE' was requested but not found in PATH." >&2
    else
      echo "Error: no container engine found. Install docker, podman, or nerdctl first." >&2
      echo "  macOS: brew install podman && podman machine init && podman machine start   # or Docker Desktop" >&2
      echo "  Debian/Ubuntu: apt-get install docker.io   # or: apt-get install podman" >&2
    fi
    exit 1
  fi
  echo "$engine"
}

require_python() {
  for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 &&
        "$candidate" -c 'import sys; raise SystemExit(sys.version_info < (3, 8))' >/dev/null 2>&1; then
      command -v "$candidate"
      return 0
    fi
  done
  echo "Error: Python 3.8+ is required to prepare credentials and initialize OpenObserve." >&2
  exit 1
}

check_engine_ready() {
  if ! "$1" info >/dev/null 2>&1; then
    echo "Error: container engine '$1' is installed but its daemon is not reachable." >&2
    echo "  Start Docker Desktop, 'podman machine start', or the docker/containerd service first." >&2
    exit 1
  fi
}

container_exists() {
  "$1" inspect "$CONTAINER_NAME" >/dev/null 2>&1
}

container_running() {
  [ "$("$1" inspect -f '{{.State.Running}}' "$CONTAINER_NAME" 2>/dev/null || true)" = "true" ]
}

do_download() {
  case "$(uname -s)" in
    Linux*|Darwin*) ;;
    *)
      echo "Error: unsupported OS '$(uname -s)'. Use openobserve.ps1 on Windows." >&2
      exit 1
      ;;
  esac
  local engine
  engine="$(require_engine)"
  check_engine_ready "$engine"
  if "$engine" image inspect "$OPENOBSERVE_IMAGE" >/dev/null 2>&1; then
    echo "OpenObserve image $OPENOBSERVE_IMAGE already exists, skipping pull."
    return 0
  fi
  echo "Pulling $OPENOBSERVE_IMAGE ..."
  "$engine" pull "$OPENOBSERVE_IMAGE"
}

do_start() {
  case "$(uname -s)" in
    Linux*|Darwin*) ;;
    *)
      echo "Error: unsupported OS '$(uname -s)'. Use openobserve.ps1 on Windows." >&2
      exit 1
      ;;
  esac

  local python root_password engine volume_suffix
  python="$(require_python)"
  mkdir -p "$DATA_DIR"
  root_password="$("$python" "$INITIALIZER" prepare \
    --password-file "$PASSWORD_FILE" \
    --values-file "$OTEL_VALUES_FILE" \
    --root-user-email "$ROOT_USER_EMAIL" \
    --organization "$ORGANIZATION" \
    --http-port "$HTTP_PORT")"

  if has_service_state; then
    do_stop
  fi
  engine="$(require_engine)"
  check_engine_ready "$engine"
  if ! "$engine" image inspect "$OPENOBSERVE_IMAGE" >/dev/null 2>&1; then
    echo "Pulling $OPENOBSERVE_IMAGE ..."
    "$engine" pull "$OPENOBSERVE_IMAGE"
  fi
  if container_exists "$engine"; then
    if container_running "$engine"; then
      echo "OpenObserve container $CONTAINER_NAME is already running. Stopping first..."
      do_stop
    else
      "$engine" rm "$CONTAINER_NAME" >/dev/null 2>&1 || true
    fi
  fi

  volume_suffix=""
  if [ "$(uname -s)" = Linux ] && [ -r /sys/fs/selinux/enforce ] &&
      [ "$(cat /sys/fs/selinux/enforce)" = 1 ]; then
    volume_suffix=":Z"
  fi

  echo "Starting OpenObserve container $CONTAINER_NAME on 127.0.0.1:${HTTP_PORT}..."
  bash "$SERVICE_MANAGER" start "$SERVICE_STATE" "openobserve-${HTTP_PORT}" "$WORK_DIR" \
    --container "$engine" "$OPENOBSERVE_IMAGE" -- \
    --publish "127.0.0.1:${HTTP_PORT}:5080" \
    --publish "127.0.0.1:${GRPC_PORT}:5081" \
    --volume "${DATA_DIR}:/data${volume_suffix}" \
    --memory "$CONTAINER_MEMORY" \
    --cpus "$CONTAINER_CPUS" \
    --env "ZO_DATA_DIR=/data" \
    --env "ZO_LOCAL_MODE=true" \
    --env "ZO_LOCAL_MODE_STORAGE=disk" \
    --env "ZO_ROOT_USER_EMAIL=${ROOT_USER_EMAIL}" \
    --env "ZO_ROOT_USER_PASSWORD=${root_password}" \
    --env "ZO_WEB_URL=http://127.0.0.1:${HTTP_PORT}" \
    --env "ZO_COMPACT_DATA_RETENTION_DAYS=${RETENTION_DAYS}" \
    --env "ZO_CREATE_ORG_THROUGH_INGESTION=false" \
    --env "ZO_TELEMETRY=false" \
    --env "ZO_MEM_TABLE_MAX_SIZE=512" \
    --env "ZO_MAX_FILE_SIZE_IN_MEMORY=64" \
    --env "ZO_MEMORY_CACHE_MAX_SIZE=256" \
    --env "ZO_MEMORY_CACHE_DATAFUSION_MAX_SIZE=512" \
    --env "ZO_DISK_CACHE_MAX_SIZE=4096" \
    --env "ZO_COMPACT_FAST_MODE=false" \
    --env "ZO_COMPACT_MAX_FILE_SIZE=128" \
    --env "ZO_HTTP_WORKER_NUM=2" \
    --env "ZO_FILE_MOVE_THREAD_NUM=1" \
    --env "ZO_MEM_DUMP_THREAD_NUM=1" \
    --env "ZO_FILE_MERGE_THREAD_NUM=1"
  CONTAINER_NAME=$(cat "$SERVICE_STATE/key")

  local created cid
  created=0
  while ! container_exists "$engine"; do
    created=$((created + 1))
    if [ "$created" -ge 100 ]; then
      do_stop
      echo "Error: OpenObserve container was not created." >&2
      exit 1
    fi
    sleep 0.1
  done
  cid="$("$engine" inspect -f '{{.Id}}' "$CONTAINER_NAME")"
  echo "$cid" > "$CID_FILE"

  if ! "$python" "$INITIALIZER" initialize \
      --password-file "$PASSWORD_FILE" \
      --root-user-email "$ROOT_USER_EMAIL" \
      --organization "$ORGANIZATION" \
      --base-url "http://127.0.0.1:${HTTP_PORT}" \
      --values-file "$OTEL_VALUES_FILE" \
      --http-port "$HTTP_PORT" \
      --startup-timeout "$STARTUP_TIMEOUT"; then
    echo "Error: OpenObserve initialization failed. Check '$engine logs $CONTAINER_NAME' for details." >&2
    do_stop
    exit 1
  fi

  echo "OpenObserve is ready at http://127.0.0.1:${HTTP_PORT} (organization: $ORGANIZATION)."
  echo "  Root user: $ROOT_USER_EMAIL"
  echo "  Password file: $PASSWORD_FILE"
  echo "  Persistent data: $DATA_DIR"
  echo "  Telemetry values: $OTEL_VALUES_FILE"
  echo "  Streams: atframework_traces, atframework_metrics (including spanmetrics), atframework_logs"
}

do_stop() {
  if has_service_state; then
    bash "$SERVICE_MANAGER" stop "$SERVICE_STATE"
    rm -f "$CID_FILE"
    return 0
  fi
  local engine
  if ! engine="$(find_engine)"; then
    if [ -f "$CID_FILE" ]; then
      echo "Error: no container engine available to stop $CONTAINER_NAME (state file: $CID_FILE)." >&2
      exit 1
    fi
    echo "No container state found. OpenObserve may not be running."
    return 0
  fi
  if ! container_exists "$engine"; then
    echo "OpenObserve container $CONTAINER_NAME does not exist."
    rm -f "$CID_FILE"
    return 0
  fi
  if container_running "$engine"; then
    echo "Stopping OpenObserve container $CONTAINER_NAME..."
    "$engine" stop "$CONTAINER_NAME" >/dev/null
  fi
  "$engine" rm "$CONTAINER_NAME" >/dev/null
  rm -f "$CID_FILE"
  echo "OpenObserve stopped. Persistent data and credentials were retained."
}

do_cleanup() {
  do_stop
  echo "Removing OpenObserve persistent data, credentials, and generated values from $WORK_DIR..."
  rm -rf "$WORK_DIR"
  local engine
  if engine="$(find_engine)"; then
    if "$engine" image inspect "$OPENOBSERVE_IMAGE" >/dev/null 2>&1; then
      "$engine" rmi "$OPENOBSERVE_IMAGE" >/dev/null 2>&1 ||
        echo "Warning: failed to remove image $OPENOBSERVE_IMAGE (it may be used elsewhere)." >&2
    fi
  fi
  echo "OpenObserve cleanup complete."
}

do_status() {
  if has_service_state; then
    bash "$SERVICE_MANAGER" status "$SERVICE_STATE"
    return
  fi
  if [ ! -f "$CID_FILE" ]; then
    echo "OpenObserve is not running (no container state file)."
    return 0
  fi
  local engine
  if ! engine="$(find_engine)"; then
    echo "Container state file exists ($CID_FILE) but no container engine is available."
    return 0
  fi
  if container_running "$engine"; then
    echo "OpenObserve is running at http://127.0.0.1:${HTTP_PORT} (container $CONTAINER_NAME)."
  else
    echo "OpenObserve is not running."
  fi
}

case "$COMMAND" in
  download) do_download ;;
  start) do_start ;;
  stop) do_stop ;;
  kill)
    if has_service_state; then
      bash "$SERVICE_MANAGER" kill "$SERVICE_STATE"
      rm -f "$CID_FILE"
    else
      do_stop
    fi
    ;;
  cleanup) do_cleanup ;;
  status) do_status ;;
  *)
    echo "Usage: $0 <download|start|stop|kill|cleanup|status> [options]" >&2
    echo "  --work-dir DIR           Runtime directory (default: <script>/var/local-test-env/openobserve)" >&2
    echo "  --http-port PORT         UI and OTLP/HTTP port (default: 5080)" >&2
    echo "  --grpc-port PORT         OTLP/gRPC port (default: 5081)" >&2
    echo "  --image IMAGE            OSS image (default: public.ecr.aws/zinclabs/openobserve:v1.1.2)" >&2
    echo "  --container-engine ENG   docker, podman, or nerdctl (default: auto-detect)" >&2
    echo "  --service-manager MODE   auto, systemd, supervisord, or none (default: auto)" >&2
    echo "  --memory LIMIT           Container memory limit (default: 3g)" >&2
    echo "  --cpus LIMIT             Container CPU limit (default: 2)" >&2
    echo "  --organization NAME      Development organization (default: atsf4g)" >&2
    echo "  --root-user-email EMAIL  Root user (default: admin@owent.net)" >&2
    echo "  --retention-days DAYS    Data retention, minimum 3 (default: 14)" >&2
    echo "  --startup-timeout SECS   Health/initialization timeout (default: 120)" >&2
    exit 1
    ;;
esac
