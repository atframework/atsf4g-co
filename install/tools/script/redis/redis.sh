#!/bin/sh
# Copyright 2026 atframework
# POSIX shell script to download (pull), start, stop, and manage redis for unit testing
# on Linux and macOS. Windows users should use redis.ps1 instead.
# redis has no official binary release for Linux/macOS, so the Docker Official Image is
# used through a local container engine (docker, podman or nerdctl, auto-detected in
# this order). No root or system packages are required.
#
# Usage: redis.sh <command> [options]
#   --work-dir DIR           Working directory (default: ${TMPDIR:-/tmp}/redis-unit-test)
#   --client-port PORT       Listening port (default: 6379)
#   --redis-version VER      Image tag to use (default: 7.2.16, the last
#                            BSD-3-Clause release line)
#   --container-engine ENG   Container engine to use (docker|podman|nerdctl;
#                            default: auto-detect)

set -eu

WORK_DIR="${TMPDIR:-/tmp}/redis-unit-test"
CLIENT_PORT=6379
REDIS_VERSION="7.2.16"
CONTAINER_ENGINE=""

# Parse command
COMMAND="${1:-}"
if [ $# -gt 0 ]; then
  shift
fi

# Parse options
while [ $# -gt 0 ]; do
  case "$1" in
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --client-port)
      CLIENT_PORT="$2"
      shift 2
      ;;
    --redis-version)
      REDIS_VERSION="$2"
      shift 2
      ;;
    --container-engine)
      CONTAINER_ENGINE="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

# 7.2.x is the last BSD-3-Clause licensed release line and resolves "latest".
if [ "$REDIS_VERSION" = "latest" ]; then
  REDIS_VERSION="7.2.16"
fi

REDIS_IMAGE="redis:${REDIS_VERSION}"
CONTAINER_NAME="atsf4g-redis-unit-test"
CID_FILE="${WORK_DIR}/redis.cid"

# Prints the engine command name on stdout; returns 1 when none is available.
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

# Resolves the engine or aborts with guidance.
require_engine() {
  local engine
  if ! engine="$(find_engine)"; then
    if [ -n "$CONTAINER_ENGINE" ]; then
      echo "Error: container engine '$CONTAINER_ENGINE' was requested but not found in PATH." >&2
    else
      echo "Error: no container engine found. redis runs in a container on Linux/macOS; install docker or podman first." >&2
      echo "  macOS: brew install podman && podman machine init && podman machine start   # or Docker Desktop" >&2
      echo "  Debian/Ubuntu: apt-get install docker.io   # or: apt-get install podman" >&2
    fi
    exit 1
  fi
  echo "$engine"
}

check_engine_ready() {
  if ! "$1" info >/dev/null 2>&1; then
    echo "Error: container engine '$1' is installed but its daemon is not reachable." >&2
    echo "  Start it first (Docker Desktop on macOS, 'podman machine start', or the docker/containerd service)." >&2
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
      echo "Error: unsupported OS '$(uname -s)'. Use redis.ps1 on Windows." >&2
      exit 1
      ;;
  esac

  local engine
  engine="$(require_engine)"
  check_engine_ready "$engine"

  if "$engine" image inspect "$REDIS_IMAGE" >/dev/null 2>&1; then
    echo "Redis image $REDIS_IMAGE already exists, skipping pull."
    echo "Use 'cleanup' command first if you want to re-pull."
    return 0
  fi

  echo "Pulling $REDIS_IMAGE ..."
  "$engine" pull "$REDIS_IMAGE"

  echo "Redis image pulled successfully."
}

do_start() {
  local engine
  engine="$(require_engine)"
  check_engine_ready "$engine"

  if ! "$engine" image inspect "$REDIS_IMAGE" >/dev/null 2>&1; then
    echo "Redis image not found. Pulling first..."
    echo "Pulling $REDIS_IMAGE ..."
    "$engine" pull "$REDIS_IMAGE"
  fi

  if container_exists "$engine"; then
    if container_running "$engine"; then
      echo "Redis container $CONTAINER_NAME is already running. Stopping first..."
      do_stop
    else
      "$engine" rm "$CONTAINER_NAME" >/dev/null 2>&1 || true
    fi
  fi

  mkdir -p "$WORK_DIR"

  echo "Starting Redis container $CONTAINER_NAME on port $CLIENT_PORT..."

  local cid
  cid="$("$engine" run -d --name "$CONTAINER_NAME" -p "127.0.0.1:${CLIENT_PORT}:6379" "$REDIS_IMAGE")"
  echo "$cid" > "$CID_FILE"
  echo "Redis container started (ID: $cid)"

  # Health check with retries; redis-cli is available inside the official image
  local max_retries=15
  local retry_count=0
  local healthy=0
  local result

  echo "Waiting for Redis to respond to PING..."
  while [ "$retry_count" -lt "$max_retries" ]; do
    sleep 1
    retry_count=$((retry_count + 1))

    result="$("$engine" exec "$CONTAINER_NAME" redis-cli PING 2>/dev/null || true)"
    if [ "$result" = "PONG" ]; then
      healthy=1
      break
    fi

    if ! container_running "$engine"; then
      echo "Error: redis container died during startup. Check '$engine logs $CONTAINER_NAME' for details." >&2
      exit 1
    fi
  done

  if [ "$healthy" -eq 1 ]; then
    echo "Redis is healthy and ready on 127.0.0.1:${CLIENT_PORT}"
  else
    echo "Error: redis failed to respond to PING within ${max_retries}s. Check '$engine logs $CONTAINER_NAME' for details." >&2
    do_stop
    exit 1
  fi
}

do_stop() {
  local engine
  if ! engine="$(find_engine)"; then
    if [ -f "$CID_FILE" ]; then
      echo "Error: no container engine available to stop $CONTAINER_NAME (state file: $CID_FILE)." >&2
      exit 1
    fi
    echo "No container state found. Redis may not be running."
    return 0
  fi

  if ! container_exists "$engine"; then
    echo "Redis container $CONTAINER_NAME does not exist."
    rm -f "$CID_FILE"
    return 0
  fi

  if container_running "$engine"; then
    echo "Stopping Redis container $CONTAINER_NAME..."
    "$engine" stop "$CONTAINER_NAME" >/dev/null
  fi

  "$engine" rm "$CONTAINER_NAME" >/dev/null

  rm -f "$CID_FILE"
  echo "Redis stopped."
}

do_cleanup() {
  do_stop

  echo "Cleaning up $WORK_DIR..."
  rm -rf "$WORK_DIR"

  local engine
  if engine="$(find_engine)"; then
    if "$engine" image inspect "$REDIS_IMAGE" >/dev/null 2>&1; then
      if ! "$engine" rmi "$REDIS_IMAGE" >/dev/null 2>&1; then
        echo "Warning: failed to remove image $REDIS_IMAGE (it may be used elsewhere)." >&2
      fi
    fi
  fi

  echo "Cleanup complete."
}

do_status() {
  if [ ! -f "$CID_FILE" ]; then
    echo "Redis is not running (no container state file)."
    return 0
  fi

  local engine
  if ! engine="$(find_engine)"; then
    echo "Container state file exists ($CID_FILE) but no container engine is available."
    return 0
  fi

  if ! container_exists "$engine"; then
    echo "Redis is not running (container $CONTAINER_NAME not found)."
    return 0
  fi

  if ! container_running "$engine"; then
    echo "Redis container $CONTAINER_NAME exists but is not running."
    return 0
  fi

  echo "Redis is running (container $CONTAINER_NAME)."
  local result
  result="$("$engine" exec "$CONTAINER_NAME" redis-cli PING 2>/dev/null || true)"
  if [ "$result" = "PONG" ]; then
    echo "Ping Response: $result"
  else
    echo "Ping check failed."
  fi
}

case "$COMMAND" in
  download) do_download ;;
  start) do_start ;;
  stop) do_stop ;;
  cleanup) do_cleanup ;;
  status) do_status ;;
  *)
    echo "Usage: $0 <download|start|stop|cleanup|status> [options]" >&2
    echo "" >&2
    echo "Options:" >&2
    echo "  --work-dir DIR           Working directory (default: \${TMPDIR:-/tmp}/redis-unit-test)" >&2
    echo "  --client-port PORT       Listening port (default: 6379)" >&2
    echo "  --redis-version VER      Image tag to use (default: 7.2.16)" >&2
    echo "  --container-engine ENG   Container engine (docker|podman|nerdctl; default: auto-detect)" >&2
    exit 1
    ;;
esac
