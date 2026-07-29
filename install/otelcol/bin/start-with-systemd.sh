#!/bin/bash

cd "$(dirname "$0")"

if [[ "x$RUN_USER" == "x" ]]; then
  RUN_USER=$(id -un)
fi

if [[ "root" == "$(id -un)" ]]; then
  SYSTEMD_SERVICE_DIR=/lib/systemd/system
else
  SYSTEMD_SERVICE_DIR="$HOME/.config/systemd/user"
  mkdir -p "$SYSTEMD_SERVICE_DIR"
fi

if [[ "$SYSTEMD_SERVICE_DIR" == "/lib/systemd/system" ]]; then
  systemctl --all | grep -F otelcol.service >/dev/null 2>&1
  if [ $? -eq 0 ]; then
    systemctl stop otelcol.service
    systemctl disable otelcol.service
  fi
else
  export XDG_RUNTIME_DIR="/run/user/$UID"
  export DBUS_SESSION_BUS_ADDRESS="unix:path=${XDG_RUNTIME_DIR}/bus"

  # Maybe need run from host: loginctl enable-linger tools
  # see https://wiki.archlinux.org/index.php/Systemd/User
  # sudo loginctl enable-linger $RUN_USER
  systemctl --user --all | grep -F otelcol.service >/dev/null 2>&1
  if [ $? -eq 0 ]; then
    systemctl --user stop otelcol.service
    systemctl --user disable otelcol.service
  fi
fi

ln -f ../etc/otelcol.systemd.service "$SYSTEMD_SERVICE_DIR/otelcol.service" || cp -f ../etc/otelcol.systemd.service "$SYSTEMD_SERVICE_DIR/otelcol.service"

sed -i "s;ExecStart=.*;ExecStart=/bin/bash $PWD/start.sh;" "$SYSTEMD_SERVICE_DIR/otelcol.service"

if [[ "$SYSTEMD_SERVICE_DIR" == "/lib/systemd/system" ]]; then
  systemctl daemon-reload
  systemctl enable otelcol.service
  systemctl start otelcol.service
else
  systemctl --user daemon-reload
  systemctl --user enable "$SYSTEMD_SERVICE_DIR/otelcol.service"
  systemctl --user start otelcol.service
fi

echo -e "Please using \\033[31mjournalctl -e --user-unit \\033[31;1motelcol\\033[0m too see the log."
