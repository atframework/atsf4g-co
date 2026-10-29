{{- define "atapp.start.prepare.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -L)"
cd "$SCRIPT_DIR"

echo "[$(date -u '+%F %T')]: {{ include "libapp.name" . }}-{{ $bus_addr }} -- Prepare start"

export PROJECT_INSTALL_DIR=$(cd ../.. && pwd)

source "$PROJECT_INSTALL_DIR/tools/script/common/common.sh"

if [[ -e "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" ]] && [[ -e "$SCRIPT_DIR/package-version.txt" ]]; then
  CURRENT_PREPARE_PACKAGE_SHOR_SHA="$(cat "$SCRIPT_DIR/package-version.txt" | grep vcs_short_sha | awk '{print $NF}')"
  find "$PROJECT_INSTALL_DIR/tools/script" -mindepth 1 -maxdepth 1 -name "prepare-package.*.lock" | grep -v -F "$CURRENT_PREPARE_PACKAGE_SHOR_SHA" | xargs -r rm -f
  if command -v flock >/dev/null 2>&1; then
    flock -x -w 20 "$PROJECT_INSTALL_DIR/tools/script/prepare-package.$CURRENT_PREPARE_PACKAGE_SHOR_SHA.lock" bash "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" "$PROJECT_INSTALL_DIR" "$CURRENT_PREPARE_PACKAGE_SHOR_SHA"
  else
    # Git Bash on Windows has no flock; run without the advisory lock.
    bash "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" "$PROJECT_INSTALL_DIR" "$CURRENT_PREPARE_PACKAGE_SHOR_SHA"
  fi
fi

SERVER_PID_FILE_NAME="{{ .Values.type_name }}_{{ $bus_addr }}.pid"
SERVICE_STATE_DIR="$SCRIPT_DIR/${SERVER_PID_FILE_NAME}.service"
SERVICE_MANAGER="$PROJECT_INSTALL_DIR/tools/script/service-manager.sh"
ATAPP_FOREGROUND=0
if [[ "${1:-}" == --atapp-foreground ]]; then
  ATAPP_FOREGROUND=1
  shift
fi

echo "[$(date -u '+%F %T')]: {{ include "libapp.name" . }}-{{ $bus_addr }} -- Prepare done"

# Early checking
if [[ -e "$SERVER_PID_FILE_NAME" ]]; then
  PROC_PID=$(cat $SERVER_PID_FILE_NAME 2>/dev/null)
else
  PROC_PID=0
fi
[[ "$PROC_PID" =~ ^[0-9]+$ ]] || PROC_PID=0
if [[ $ATAPP_FOREGROUND -eq 0 && ! -f "$SERVICE_STATE_DIR/backend" && ! -f "$SERVICE_STATE_DIR/lifecycle.record" && ! -f "$SERVICE_STATE_DIR/lifecycle.record.bak" && ! -f "$SERVICE_STATE_DIR/unit" && $PROC_PID -gt 0 ]] && [[ 0 -eq $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] ; then
  NoticeMsg "[$(date -u '+%F %T')]: {{ .Values.proc_name }} - {{ $bus_addr }} already started, skipped.";
  exit 0
fi

if [[ $ATAPP_FOREGROUND -eq 1 && $PROC_PID -gt 0 ]]; then
  cp -f "$SERVER_PID_FILE_NAME" "$SERVER_PID_FILE_NAME.old"
fi
{{- end }}
