#!/bin/bash
# Execute one saved business stop command per process lifetime.
set -euo pipefail
state="$1"; shift
if [[ -f "$state/force-kill" || ! -s "$state/stop-script" ]]; then exit 0; fi
if command -v flock >/dev/null; then
  exec 7>"$state/stop-command.lock"
  flock -x -w 30 7
else
  attempts=0
  until mkdir "$state/stop-command.lock.d" 2>/dev/null; do
    ((attempts+=1))
    [[ $attempts -lt 300 ]] || { echo 'Business stop lock timeout' >&2; exit 1; }
    sleep 0.1
  done
  trap 'rmdir "$state/stop-command.lock.d"' EXIT
fi
if [[ -f "$state/force-kill" ]]; then exit 0; fi
if [[ -f "$state/process.record" && -f "$state/stop-command.done" ]] &&
    cmp -s "$state/process.record" "$state/stop-command.done"; then exit 0; fi
if [[ $# -eq 0 && -f "$state/stop-arguments.sh" ]]; then source "$state/stop-arguments.sh"; fi
rm -f "$state/stop-command.error"
if /bin/bash "$state/stop-command.sh" "$@"; then
  if [[ -f "$state/process.record" ]]; then
    cp "$state/process.record" "$state/stop-command.done.tmp"
    mv -f "$state/stop-command.done.tmp" "$state/stop-command.done"
  else touch "$state/stop-command.done"; fi
else
  result=$?
  printf '%s\n' "$result" > "$state/stop-command.error"
  exit "$result"
fi
