#!/bin/bash
# Mount into an existing atsf4g image and run as its entrypoint. The original
# /entrypoint.sh still prepares configuration and watches it for reloads.
set -u

if [[ "${1:-}" != start ]]; then
  exec /bin/bash /entrypoint.sh "$@"
fi

stop_container() {
  trap '' TERM INT
  /bin/bash /entrypoint.sh stop
  # Exiting the container's main process also reaps any remaining watcher children.
  exit 0
}
trap stop_container TERM INT

/bin/bash /entrypoint.sh "$@" &
entrypoint_pid=$!
startup_timeout="${ATF_CONTAINER_START_TIMEOUT:-180}"
[[ "$startup_timeout" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid ATF_CONTAINER_START_TIMEOUT' >&2; exit 1; }
deadline=$((SECONDS + startup_timeout))

while ! /bin/bash /entrypoint.sh health_check >/dev/null 2>&1; do
  if ! kill -0 "$entrypoint_pid" 2>/dev/null || [[ $SECONDS -ge $deadline ]]; then
    echo '[container-entrypoint] service did not become healthy' >&2
    exit 1
  fi
  sleep 1
done

echo '[container-entrypoint] service is healthy' >&2
while kill -0 "$entrypoint_pid" 2>/dev/null; do
  if ! /bin/bash /entrypoint.sh health_check >/dev/null 2>&1; then
    echo '[container-entrypoint] service exited; stopping container for supervisor restart' >&2
    exit 1
  fi
  sleep 1
done

echo '[container-entrypoint] configuration watcher exited' >&2
exit 1
