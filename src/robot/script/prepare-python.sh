#!/bin/sh
# POSIX counterpart of prepare-python.ps1: install optional requirements into the robot
# venv and register the venv python in build-settings.
set -e

PYTHON_VENV="$1"
BUILD_SETTING_BIN="$2"
SETTINGS_FILE="$3"
REQUIREMENTS_FILE="$4"

if [ -z "$PYTHON_VENV" ] || [ -z "$BUILD_SETTING_BIN" ] || [ -z "$SETTINGS_FILE" ]; then
  echo "Usage: prepare-python.sh <venv-dir> <build_setting-bin> <settings-file> [requirements-file]" >&2
  exit 1
fi

PYTHON_BIN="$PYTHON_VENV/bin/python"

if [ -n "$REQUIREMENTS_FILE" ]; then
  if [ -f "$REQUIREMENTS_FILE" ]; then
    echo "Installing Python dependencies..."
    "$PYTHON_BIN" -m pip install -r "$REQUIREMENTS_FILE"
    echo "Dependencies installed"
  else
    echo "requirements.txt not found at $REQUIREMENTS_FILE"
  fi
fi

PYTHON_VERSION=$("$PYTHON_BIN" --version 2>&1 | awk '{print $2}')
"$BUILD_SETTING_BIN" set python -path "$PYTHON_BIN" -version "$PYTHON_VERSION" -settings-file "$SETTINGS_FILE"
echo "Python Version: $PYTHON_VERSION environment OK!"
