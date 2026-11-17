#!/bin/sh
# Copyright 2026 atframework
# Start the local test environment (etcd, Redis, and OpenObserve) for development.
# Windows users should use start_local_test_env.ps1 instead.
# Missing binaries/images are downloaded automatically on first use.

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
"$SCRIPT_DIR/openobserve/openobserve.sh" start
