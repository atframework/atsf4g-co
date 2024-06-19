#!/bin/bash

function WaitForMS() {
  if [[ $# -lt 1 ]]; then
    return 0
  fi

  WAITTIME_MS=$1
  which usleep >/dev/null 2>&1
  if [[ 0 -eq $? ]]; then
    let WAITTIME_MS=$WAITTIME_MS*1000
    usleep $WAITTIME_MS
    return 0
  fi

  which python3 >/dev/null 2>&1
  if [[ 0 -eq $? ]]; then
    python3 -c "import time; time.sleep($WAITTIME_MS / 1000.0)"
    return 0
  fi

  which python >/dev/null 2>&1
  if [[ 0 -eq $? ]]; then
    python -c "import time; time.sleep($WAITTIME_MS / 1000.0)"
    return 0
  fi

  return 1
}

function CheckProcessRunning() {
  # parameters: <pid file> [except pid]
  if [[ $# -lt 1 ]]; then
    return 0
  fi
  PID_FILE="$1"
  if [[ $# -gt 1 ]]; then
    PROC_EXPECT_PID=$2
  else
    PROC_EXPECT_PID=""
  fi

  if [[ ! -f "$PID_FILE" ]]; then
    return 0
  fi

  PROC_PID=$(cat "$PID_FILE" 2>/dev/null)

  if [[ "x$PROC_PID" != "x" ]] && [[ $PROC_PID -le 0 ]]; then
    return 2
  fi

  if [[ "x$PROC_EXPECT_PID" != "x" ]] && [[ "x$PROC_PID" != "x$PROC_EXPECT_PID" ]]; then
    return 0
  fi

  SYSFLAGS=($PROC_PID)
  if [[ "x${MSYSTEM}" != "x" ]]; then
    SYSFLAGS=($PROC_PID -W)
  fi
  if [[ ! -z "$PROC_PID" ]] && [[ ! -z "$(ps -p ${SYSFLAGS[@]} 2>&1 | grep $PROC_PID)" ]]; then
    return 1
  fi

  return 0
}

function WaitProcessStarted() {
  # parameters: <pid file> [wait time] [except pid] [startup error file]
  if [[ $# -lt 1 ]]; then
    return 1
  fi

  WAIT_TIME=5000
  PID_FILE="$1"

  if [[ $# -gt 1 ]]; then
    WAIT_TIME=$2
  fi

  if [[ $# -gt 2 ]]; then
    PROC_EXPECT_PID=$3
  else
    PROC_EXPECT_PID=""
  fi

  if [[ $# -gt 3 ]]; then
    PROC_STARTUP_ERROR_FILE="$4"
  else
    PROC_STARTUP_ERROR_FILE=""
  fi

  while [[ $WAIT_TIME -gt 0 ]]; do
    CheckProcessRunning "$PID_FILE" "$PROC_EXPECT_PID" "$PROC_STARTUP_ERROR_FILE"
    CheckResult=$?
    if [[ 1 -eq $CheckResult ]]; then
      return 0
    fi

    if [[ 2 -eq $CheckResult ]]; then
      return 3
    fi

    WaitForMS 100
    let WAIT_TIME=$WAIT_TIME-100
  done

  return 2
}

function WaitProcessStoped() {
  # parameters: <pid file> [wait time]
  if [[ $# -lt 1 ]]; then
    return 1
  fi

  WAIT_TIME=10000
  PID_FILE="$1"

  if [[ $# -gt 1 ]]; then
    WAIT_TIME=$2
  fi

  while [[ 1 -eq 1 ]]; do
    CheckProcessRunning "$PID_FILE"
    if [[ 1 -ne $? ]]; then
      return 0
    fi

    if [[ $WAIT_TIME -gt 0 ]]; then
      WaitForMS 100
      let WAIT_TIME=$WAIT_TIME-100
    else
      return 2
    fi
  done

  return 0
}
