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

exit 0
