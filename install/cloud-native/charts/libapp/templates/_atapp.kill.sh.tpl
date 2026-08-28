{{- define "atapp.kill.sh" -}}
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
    NoticeMsg "{{ .Values.proc_name }} - {{ $bus_addr }} already stopped";
    exit 0;
fi

PROC_PID=$(cat "$SERVER_PID_FILE_NAME" 2>/dev/null);

if [[ 0 -eq $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] ; then
    proc_pid=$(ps aux | grep "$SCRIPT_DIR/{{ .Values.proc_name }}" | grep -w "{{ .Values.type_name }}_{{ $bus_addr }}.pid" | grep -v grep | awk '{print $2}')
    if [ "x${proc_pid}" != "x" ]; then
        kill -9 $proc_pid
    fi

    CheckProcessRunning "$SERVER_PID_FILE_NAME";
    if [[ 0 -ne $? ]]; then
        ErrorMsg "kill -9 {{ .Values.proc_name }} - {{ $bus_addr }} failed." ;
    else
        NoticeMsg "kill -9 {{ .Values.proc_name }} - {{ $bus_addr }} done." ;
    fi
else
    NoticeMsg "{{ .Values.proc_name }} - {{ $bus_addr }} not running, skipped.";
fi
{{- end }}
