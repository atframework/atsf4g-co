{{- define "atapp.start.prepare.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPT_DIR="$(readlink -f $SCRIPT_DIR)"
cd "$SCRIPT_DIR"

echo "[$(date -u '+%F %T')]: {{ include "libapp.name" . }}-{{ $bus_addr }} -- Prepare start"

export PROJECT_INSTALL_DIR=$(cd ../.. && pwd)

source "$PROJECT_INSTALL_DIR/tools/script/common/common.sh"

if [[ -e "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" ]] && [[ -e "$SCRIPT_DIR/package-version.txt" ]]; then
  CURRENT_PREPARE_PACKAGE_SHOR_SHA="$(cat "$SCRIPT_DIR/package-version.txt" | grep vcs_short_sha | awk '{print $NF}')"
  find "$PROJECT_INSTALL_DIR/tools/script" -mindepth 1 -maxdepth 1 -name "prepare-package.*.lock" | grep -v -F "$CURRENT_PREPARE_PACKAGE_SHOR_SHA" | xargs -r rm -f
  flock -x -w 20 "$PROJECT_INSTALL_DIR/tools/script/prepare-package.$CURRENT_PREPARE_PACKAGE_SHOR_SHA.lock" bash "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" "$PROJECT_INSTALL_DIR" "$CURRENT_PREPARE_PACKAGE_SHOR_SHA"
fi

SERVER_PID_FILE_NAME="{{ .Values.type_name }}_{{ $bus_addr }}.pid"

echo "[$(date -u '+%F %T')]: {{ include "libapp.name" . }}-{{ $bus_addr }} -- Prepare done"

# Early checking
if [[ -e "$SERVER_PID_FILE_NAME" ]]; then
  PROC_PID=$(cat $SERVER_PID_FILE_NAME 2>/dev/null)
else
  PROC_PID=0
fi
if [[ $PROC_PID -gt 0 ]]  && [[ 0 -eq $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] ; then
  NoticeMsg "[$(date -u '+%F %T')]: {{ .Values.proc_name }} - {{ $bus_addr }} already started, skipped.";
  exit 0
fi

exec 256<> "${SERVER_PID_FILE_NAME}.start.lock"
flock -x -w 30 256

if [[ $? -ne 0 ]]; then
  ErrorMsg "[$(date -u '+%F %T')]: lock ${SERVER_PID_FILE_NAME}.start.lock failed.";
fi

if [[ -e "$SERVER_PID_FILE_NAME" ]]; then
  PROC_PID=$(cat $SERVER_PID_FILE_NAME 2>/dev/null)
else
  PROC_PID=0
fi
if [[ $PROC_PID -gt 0 ]]; then
  cp -f "$SERVER_PID_FILE_NAME" "$SERVER_PID_FILE_NAME.old"
fi
{{- end }}
