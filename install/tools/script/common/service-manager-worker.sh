#!/bin/bash
# Supervisor signals this adapter; only the saved stop command stops the server.
set -u
state="$1"
stop_requested() {
  /bin/bash "$state/stop.sh"
}
trap stop_requested TERM INT
/bin/bash "$state/command.sh" &
child=$!
while :; do
  wait "$child"
  if ! kill -0 "$child" 2>/dev/null; then
    # A stop trap can interrupt wait after the child has already exited.
    wait "$child"
    exit $?
  fi
done
