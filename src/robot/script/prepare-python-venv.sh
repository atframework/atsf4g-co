#!/bin/sh
# POSIX counterpart of prepare-python-venv.ps1: create the robot venv and recover
# a directory left incomplete by an interrupted run.
set -e

PYTHON_CMD="$1"
PYTHON_VENV="$2"
REQUIREMENTS_FILE="$3"

if [ -z "$PYTHON_CMD" ] || [ -z "$PYTHON_VENV" ]; then
  echo "Usage: prepare-python-venv.sh <python-command> <venv-dir> [requirements-file]" >&2
  exit 1
fi

if [ -d "$PYTHON_VENV" ]; then
  VENV_RESOLVED=$(cd "$PYTHON_VENV" && pwd -P)
  if [ "$VENV_RESOLVED" = "/" ]; then
    echo "Refusing unsafe venv dir: $PYTHON_VENV" >&2
    exit 1
  fi
  if ! "$PYTHON_VENV/bin/python3" -m pip --version > /dev/null 2>&1; then
    echo "Removing broken Python venv..."
    rm -rf "$PYTHON_VENV"
  fi
elif [ -e "$PYTHON_VENV" ]; then
  echo "Python venv path exists but is not a directory: $PYTHON_VENV" >&2
  exit 1
fi

if [ ! -d "$PYTHON_VENV" ]; then
  echo "Creating Python venv at $PYTHON_VENV..."
  "$PYTHON_CMD" -m venv "$PYTHON_VENV"
  echo "venv created successfully"
else
  echo "Python venv already exists at $PYTHON_VENV"
fi

if [ -n "$REQUIREMENTS_FILE" ]; then
  "$PYTHON_VENV/bin/python3" -m pip install -r "$REQUIREMENTS_FILE"
fi
