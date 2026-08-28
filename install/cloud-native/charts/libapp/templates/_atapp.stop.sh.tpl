{{- define "atapp.stop.sh" -}}
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
REAL_SERVER_PID_FILE_NAME="$SERVER_PID_FILE_NAME"

if [[ -e "$SERVER_PID_FILE_NAME" ]]; then
  PROC_PID=$(cat $SERVER_PID_FILE_NAME 2>/dev/null)
else
  PROC_PID=0
fi
if [[ $PROC_PID -le 0 ]] && [[ -e "$SERVER_PID_FILE_NAME.old" ]]; then
  REAL_SERVER_PID_FILE_NAME="$SERVER_PID_FILE_NAME.old"
  PROC_PID=$(cat "$REAL_SERVER_PID_FILE_NAME" 2>/dev/null)
fi

CheckProcessRunning "$REAL_SERVER_PID_FILE_NAME";
if [[ 0 -eq $? ]] && [[ $PROC_PID -gt 0 ]]; then
    NoticeMsg "{{ .Values.proc_name }} - {{ $bus_addr }} - pid $PROC_PID already stopped";
    exit 0;
fi

SERVER_STARTUP_ERROR_FILE_NAME="${SERVER_PID_FILE_NAME/.pid/}.startup-error"
if [ -e "$SERVER_STARTUP_ERROR_FILE_NAME" ]; then
  rm -f "$SERVER_STARTUP_ERROR_FILE_NAME"
fi

{{ include "libapp.run.wrapper.sh" . }}

if [[ $PROC_PID -le 0 ]] || [[ 0 -eq $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] ; then

    # prestop
    # atapp_run_wrapper "$SCRIPT_DIR/{{ .Values.proc_name }}" -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml run prestop

    # if [[ $? -ne 0 ]]; then
    #     ErrorMsg "send prestop command to {{ .Values.proc_name }} - {{ $bus_addr }} failed.";
    # else
    #     WAIT_TIME=6000
    #     PRESTOP_WAIT_TIME=4000
    #     while [[ 1 -eq 1 ]]; do
    #         if [[ $PRESTOP_WAIT_TIME -lt 0 ]];then
    #             PRESTOP_WAIT_TIME=4000
    #             atapp_run_wrapper "$SCRIPT_DIR/{{ .Values.proc_name }}" -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml run prestop
    #             if [[ $? -ne 0 ]]; then
    #                 ErrorMsg "send prestop command to {{ .Values.proc_name }} - {{ $bus_addr }} failed.";
    #                 break
    #             fi
    #         fi

    #         status=$(atapp_run_wrapper "$SCRIPT_DIR/{{ .Values.proc_name }}" -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml run prestop_check | grep "server prestop success" | wc -l)
    #         if [[ "1" -eq "${status}" ]]; then
    #             NoticeMsg "prestop {{ .Values.proc_name }} - {{ $bus_addr }} done." ;
    #             break
    #         fi

    #         if [[ $WAIT_TIME -gt 0 ]]; then
    #             WaitForMS 100
    #             let WAIT_TIME=$WAIT_TIME-100
    #             let PRESTOP_WAIT_TIME=$PRESTOP_WAIT_TIME-100
    #         else
    #             ErrorMsg "prestop {{ .Values.proc_name }} - {{ $bus_addr }} failed.Force stop." ;
    #             break
    #         fi
    #     done
    # fi

    atapp_run_wrapper "$SCRIPT_DIR/{{ .Values.proc_name }}" -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml stop "$@"

    if [[ $? -ne 0 ]]; then
        ErrorMsg "send stop command to {{ .Values.proc_name }} - {{ $bus_addr }} failed.";
    else
        WaitProcessStoped "$REAL_SERVER_PID_FILE_NAME";
    fi

    CheckProcessRunning "$REAL_SERVER_PID_FILE_NAME";
    if [[ 0 -ne $? ]] && [[ $PROC_PID -gt 0 ]]; then
        NoticeMsg "{{ .Values.proc_name }} - {{ $bus_addr }} can not be stoped by command, try to kill by signal";
        kill $(cat "$REAL_SERVER_PID_FILE_NAME");
        WaitProcessStoped "$REAL_SERVER_PID_FILE_NAME";
    fi

    NoticeMsg "stop {{ .Values.proc_name }} - {{ $bus_addr }} done." ;
else
    NoticeMsg "{{ .Values.proc_name }} - {{ $bus_addr }} not running, skipped.";
fi
{{- end }}
