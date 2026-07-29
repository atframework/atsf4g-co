#!/bin/bash

cd "$(dirname "$0")"

source ./otelcol-common.sh

export SYSTEMD_CAT_IDENTIFIER=otelcol

OTELCOL_CURRENT_BIN="$(readlink -f otelcol-contrib)"

# Check from pidfile
if [[ -e "otelcol-contrib.pid" ]]; then
  OTELCOL_TEST_BIN="$(readlink -f /proc/$(cat otelcol-contrib.pid)/exe)"
  OTELCOL_TEST_BIN="${OTELCOL_TEST_BIN% (deleted)}"
  if [[ "$OTELCOL_CURRENT_BIN" == "$OTELCOL_TEST_BIN" ]]; then
    echo "otelcol-contrib already started"
    exit 0
  fi
fi

# Check from ps
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

if [[ $OTELCOL_FOUND -ne 0 ]]; then
  echo "otelcol-contrib already started"
  exit 0
fi

bash ./start.sh &

WAIT_TIME=20000
OTELCOL_START_SUCCESS=0

while [[ $WAIT_TIME -gt 0 ]] && [[ $OTELCOL_START_SUCCESS -eq 0 ]]; do
  OTELCOL_PIDS=($(ps ux | grep otelcol-contrib | grep -v grep | awk '{print $2}'))
  for OTELCOL_PID in ${OTELCOL_PIDS[@]}; do
    OTELCOL_TEST_BIN="$(readlink -f /proc/$OTELCOL_PID/exe)"
    OTELCOL_TEST_BIN="${OTELCOL_TEST_BIN% (deleted)}"
    if [[ "$OTELCOL_CURRENT_BIN" == "$OTELCOL_TEST_BIN" ]]; then
      OTELCOL_START_SUCCESS=1
      echo "$OTELCOL_PID" >otelcol-contrib.pid
      break
    fi
  done

  WaitForMS 100
  let WAIT_TIME=$WAIT_TIME-100
done

if [[ $OTELCOL_START_SUCCESS -ne 0 ]]; then
  echo "otelcol-contrib started"
else
  echo "otelcol-contrib start failed"
  exit 1
fi
