{{- define "atapp.runcmd.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

export PROJECT_INSTALL_DIR=$(cd ../.. && pwd);

source "$PROJECT_INSTALL_DIR/tools/script/common/common.sh";

if [[ -e "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" ]] && [[ -e "$SCRIPT_DIR/package-version.txt" ]]; then
  CURRENT_PREPARE_PACKAGE_SHOR_SHA="$(cat "$SCRIPT_DIR/package-version.txt" | grep vcs_short_sha | awk '{print $NF}')"
  if command -v flock >/dev/null 2>&1; then
    flock -x -w 20 "$PROJECT_INSTALL_DIR/tools/script/prepare-package.$CURRENT_PREPARE_PACKAGE_SHOR_SHA.lock" bash "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" "$PROJECT_INSTALL_DIR" "$CURRENT_PREPARE_PACKAGE_SHOR_SHA"
  else
    # Git Bash on Windows has no flock; run without the advisory lock.
    bash "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" "$PROJECT_INSTALL_DIR" "$CURRENT_PREPARE_PACKAGE_SHOR_SHA"
  fi
fi

SERVER_PID_FILE_NAME="{{ .Values.type_name }}_{{ $bus_addr }}.pid";

CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [[ 0 -eq $? ]]; then
  NoticeMsg "send run $* command to {{ .Values.proc_name }} - {{ $bus_addr }} failed, not running";
  exit 0;
fi

PROC_PID=$(cat "$SERVER_PID_FILE_NAME" 2>/dev/null);

SERVER_STARTUP_ERROR_FILE_NAME="${SERVER_PID_FILE_NAME/.pid/}.startup-error"
if [ -e "$SERVER_STARTUP_ERROR_FILE_NAME" ]; then
  rm -f "$SERVER_STARTUP_ERROR_FILE_NAME"
fi

{{ include "libapp.run.wrapper.sh" . }}

if [[ 0 -eq $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] ; then
    if [[ "$1" == "prestop" ]] ; then
        bash "$SCRIPT_DIR/prestop.sh_{{ $bus_addr }}"
        exit $?
    fi
    atapp_run_wrapper "$SCRIPT_DIR/{{ .Values.proc_name }}" -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml run "$@"
else
    NoticeMsg "send run $* command to {{ .Values.proc_name }} - {{ $bus_addr }} failed, not running";
fi
{{- end }}
