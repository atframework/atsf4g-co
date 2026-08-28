#!/bin/sh
# Copyright 2026 atframework
# Start the local test environment (etcd, redis and otel collector) for unit testing
# on Linux and macOS. Windows users should use start_local_test_env.ps1 instead.
# etcd and redis are downloaded automatically on first use.

set -eu

case "$(uname -s)" in
  Linux*|Darwin*) ;;
  *)
    echo "Error: unsupported OS '$(uname -s)'. Use start_local_test_env.ps1 on Windows." >&2
    exit 1
    ;;
esac

SCRIPT_DIR="$(CDPATH='' cd "$(dirname -- "$0")" && pwd)"

"$SCRIPT_DIR/etcd/setup-etcd.sh" start
"$SCRIPT_DIR/redis/redis.sh" start

# The otel collector binary is downloaded by the CMake build
# (third_party/otel/otelcol-contrib.cmake); it is not managed here.
OTELCOL_BIN_DIR="${SCRIPT_DIR}/../../otelcol/bin"

cd "$OTELCOL_BIN_DIR"

if [ ! -x "./otelcol-contrib" ]; then
  echo "[ERROR] Otel collector executable does not exist or is not executable:" >&2
  echo "        ${OTELCOL_BIN_DIR}/otelcol-contrib" >&2
  exit 1
fi

if [ ! -f "../cfg/config.yaml" ]; then
  echo "[ERROR] Otel collector config does not exist: ${OTELCOL_BIN_DIR}/../cfg/config.yaml" >&2
  echo "        Run install/tools/script/generate_config.sh first." >&2
  exit 1
fi

mkdir -p ../log

# Run in the foreground like the Windows version; stop it with Ctrl+C or by running
# stop_local_test_env.sh. exec makes the collector receive signals directly.
exec ./otelcol-contrib --config=../cfg/config.yaml
