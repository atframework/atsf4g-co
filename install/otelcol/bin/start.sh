#!/bin/bash

cd "$(dirname "$0")"

mkdir -p ../log

if [[ ! -z "$SYSTEMD_CAT_IDENTIFIER" ]]; then

  # Using journalctl -e -t "$SYSTEMD_CAT_IDENTIFIER" to see the log

  ./otelcol-contrib --config=../etc/config.yaml 2>&1 | systemd-cat -t "$SYSTEMD_CAT_IDENTIFIER" -p info

else

  ./otelcol-contrib --config=../etc/config.yaml

fi
