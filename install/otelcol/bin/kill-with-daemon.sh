#!/bin/bash

cd "$(dirname "$0")"

source ./otelcol-common.sh

OTELCOL_CURRENT_BIN="$(readlink -f otelcol-contrib)"

if [[ ! -e "otelcol-contrib.pid" ]]; then
  OTELCOL_PIDS=($(ps ux | grep otelcol-contrib | grep -v grep | awk '{print $2}'))
  OTELCOL_FOUND=0

  for OTELCOL_PID in ${OTELCOL_PIDS[@]}; do
    OTELCOL_TEST_BIN="$(readlink -f /proc/$OTELCOL_PID/exe)"
    OTELCOL_TEST_BIN="${OTELCOL_TEST_BIN% (deleted)}"
    if [[ "$OTELCOL_CURRENT_BIN" == "$OTELCOL_TEST_BIN" ]]; then
      OTELCOL_FOUND=1
      echo "$OTELCOL_PID" >otelcol-contrib.pid
      break
    fi
  done

  if [[ $OTELCOL_FOUND -eq 0 ]]; then
    echo "otelcol-contrib is already exited."
    exit 0
  fi
fi

OTELCOL_PID=$(cat "otelcol-contrib.pid")
if [[ -e "/proc/$OTELCOL_PID/exe" ]]; then
  OTELCOL_TEST_BIN="$(readlink -f /proc/$OTELCOL_PID/exe)"
  OTELCOL_TEST_BIN="${OTELCOL_TEST_BIN% (deleted)}"
  if [[ "$OTELCOL_CURRENT_BIN" != "$OTELCOL_TEST_BIN" ]]; then
    echo "otelcol-contrib is already exited."
    exit 0
  fi
fi

kill -9 $OTELCOL_PID

WaitProcessStoped "otelcol-contrib.pid" 20000

if [[ $? -eq 0 ]]; then
  rm -f "otelcol-contrib.pid"
fi
