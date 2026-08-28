#!/bin/sh
# Copyright 2026 atframework
# Stop the local test environment (etcd, redis and otel collector) on Linux and macOS.
# Windows users should use stop_local_test_env.ps1 instead.

set -eu

case "$(uname -s)" in
  Linux*|Darwin*) ;;
  *)
    echo "Error: unsupported OS '$(uname -s)'. Use stop_local_test_env.ps1 on Windows." >&2
    exit 1
    ;;
esac

SCRIPT_DIR="$(CDPATH='' cd "$(dirname -- "$0")" && pwd)"

"$SCRIPT_DIR/etcd/setup-etcd.sh" stop
"$SCRIPT_DIR/redis/redis.sh" stop

# Stop the otel collector. This mirrors the Windows `taskkill /F /T /IM` semantics of
# killing every instance by executable name.
OTELCOL_NAME="otelcol-contrib"

find_otelcol_pids() {
  if command -v pgrep >/dev/null 2>&1; then
    pgrep -x "$OTELCOL_NAME" 2>/dev/null || true
  else
    # Fallback: match the executable name (last field) reported by ps
    ps -e -o pid= -o comm= 2>/dev/null |
      awk -v name="$OTELCOL_NAME" '$NF == name || $NF ~ ("/" name "$") { print $1 }'
  fi
}

stop_otelcol() {
  pids="$(find_otelcol_pids)"
  if [ -z "$pids" ]; then
    echo "otelcol-contrib is not running."
    return 0
  fi

  echo "Stopping otelcol-contrib (PID: $(echo $pids | tr '\n' ' '))..."
  for pid in $pids; do
    kill "$pid" 2>/dev/null || true
  done

  waited=0
  while [ "$waited" -lt 10 ]; do
    sleep 1
    waited=$((waited + 1))
    pids="$(find_otelcol_pids)"
    if [ -z "$pids" ]; then
      echo "otelcol-contrib stopped."
      return 0
    fi
  done

  echo "Force killing otelcol-contrib..."
  for pid in $pids; do
    kill -9 "$pid" 2>/dev/null || true
  done
  echo "otelcol-contrib stopped."
}

stop_otelcol

exit 0
