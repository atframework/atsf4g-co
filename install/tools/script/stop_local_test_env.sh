#!/bin/sh
# Copyright 2026 atframework
# Stop the local test environment (OpenObserve, etcd, and Redis) on Linux and macOS.
# OpenObserve data and credentials are retained. Windows users should use the ps1 script.

set -eu

case "$(uname -s)" in
  Linux*|Darwin*) ;;
  *)
    echo "Error: unsupported OS '$(uname -s)'. Use stop_local_test_env.ps1 on Windows." >&2
    exit 1
    ;;
esac

SCRIPT_DIR="$(CDPATH='' cd "$(dirname -- "$0")" && pwd)"

"$SCRIPT_DIR/openobserve/openobserve.sh" stop
"$SCRIPT_DIR/etcd/setup-etcd.sh" stop
"$SCRIPT_DIR/redis/redis.sh" stop

exit 0
