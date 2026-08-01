#!/bin/sh
# POSIX counterpart of rpc-handle.ps1: invoke mako-generator in server-client mode.
# The base-interpreter shim is not needed on POSIX: venv python is a symlink and both
# realpath(sys.executable) and /proc/<pid>/exe resolve to the same base interpreter.
set -e

PYTHON_BIN="$1"
MAKO_GENERATOR_PY="$2"
SERVER_PID_FILE="$3"
SERVER_PORT_FILE="$4"
PACKAGE_PREFIX="$5"
PROJECT_DIR="$6"
PB_FILE="$7"
RULE_FILE="$8"

if [ -z "$PYTHON_BIN" ] || [ -z "$MAKO_GENERATOR_PY" ] || [ -z "$SERVER_PID_FILE" ] || [ -z "$SERVER_PORT_FILE" ] || [ -z "$PROJECT_DIR" ] || [ -z "$PB_FILE" ] || [ -z "$RULE_FILE" ]; then
  echo "Usage: rpc-handle.sh <python-bin> <mako-generator.py> <pid-file> <port-file> [package-prefix] <project-dir> <pb-file> <rule-file>" >&2
  exit 1
fi

set -- "$MAKO_GENERATOR_PY" \
  --server-pid-file "$SERVER_PID_FILE" \
  --server-port-file "$SERVER_PORT_FILE" \
  --server-auto-start \
  --client-mode
if [ -n "$PACKAGE_PREFIX" ]; then
  set -- "$@" --add-package-prefix "$PACKAGE_PREFIX"
fi
set -- "$@" --project-dir "$PROJECT_DIR" --pb-file "$PB_FILE" -c "$RULE_FILE"

exec "$PYTHON_BIN" "$@"
