#!/bin/bash

set -e

cd "$(dirname $0)"

# helm is installed by the CMake build into <build>/tools/helm, next to the deploy tree (<build>/publish).
# Probe that location first, then the legacy in-tree <publish>/tools/helm for older build trees.
for HELM_DIR_CANDIDATE in ../../../tools/helm ../../tools/helm; do
  if [ -d "$HELM_DIR_CANDIDATE" ]; then
    export PATH="$PATH:$(cd "$HELM_DIR_CANDIDATE" && pwd)"
    break
  fi
done

if [[ "x$HELM_BIN" == "x" ]]; then
  # helm.exe covers a Windows build tree driven from a Unix-like shell (WSL/MSYS), where `helm` alone does not resolve.
  HELM_BIN="$(command -v helm || command -v helm.exe || true)"
fi

if [[ "x$HELM_BIN" == "x" ]]; then
  echo "[ERROR] helm not found. Expected under <build>/tools/helm or on PATH." >&2
  exit 1
fi

cd ../../cloud-native/charts

servers=$(ls -l | awk '/^d/ {print $NF}')

for svr in ${servers}; do
  if [ $svr = "libapp" -o $svr = "app" ]; then
    continue
  fi
  if [[ ! -e "$svr/Chart.yaml" ]]; then
    continue
  fi
  echo $svr
  "$HELM_BIN" dependency update $svr
done