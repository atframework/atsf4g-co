{{- define "atapp.prestop.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

export PROJECT_INSTALL_DIR=$(cd ../.. && pwd);

if [[ -e "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" ]] && [[ -e "$SCRIPT_DIR/package-version.txt" ]]; then
  CURRENT_PREPARE_PACKAGE_SHOR_SHA="$(cat "$SCRIPT_DIR/package-version.txt" | grep vcs_short_sha | awk '{print $NF}')"
  flock -x -w 20 "$PROJECT_INSTALL_DIR/tools/script/prepare-package.$CURRENT_PREPARE_PACKAGE_SHOR_SHA.lock" bash "$PROJECT_INSTALL_DIR/tools/script/prepare-dependency-dll.sh" "$PROJECT_INSTALL_DIR" "$CURRENT_PREPARE_PACKAGE_SHOR_SHA"
fi

source "$PROJECT_INSTALL_DIR/tools/script/common/common.sh";

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

LAST_RUNCMD_LOGFILE="$SCRIPT_DIR/{{ .Values.proc_name }}_{{ $bus_addr }}_last_runcmd"
CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [[ 0 -eq $? ]] && [[ $PROC_PID -gt 0 ]]; then
  NoticeMsg "prestop_succeed, {{ .Values.proc_name }} - {{ $bus_addr }} - pid $PROC_PID already stopped." | tee $LAST_RUNCMD_LOGFILE
  exit 0
fi

SERVER_STARTUP_ERROR_FILE_NAME="${SERVER_PID_FILE_NAME/.pid/}.startup-error"
if [ -e "$SERVER_STARTUP_ERROR_FILE_NAME" ]; then
  rm -f "$SERVER_STARTUP_ERROR_FILE_NAME"
fi

{{ include "libapp.run.wrapper.sh" . }}

touch $LAST_RUNCMD_LOGFILE
if [[ $PROC_PID -le 0 ]] || [[ 0 -eq $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] ; then
    atapp_run_wrapper "$SCRIPT_DIR/{{ .Values.proc_name }}" -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml run prestop 0 0 > $LAST_RUNCMD_LOGFILE
    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        ErrorMsg "prestop_failed, send prestop command to {{ .Values.proc_name }} - {{ $bus_addr }} failed." | tee -a $LAST_RUNCMD_LOGFILE
        exit 1
    fi

    status=$(cat $LAST_RUNCMD_LOGFILE | grep "Run Prestop Command Failed" | wc -l)
    if [[ "0" -ne "${status}" ]]; then
        ErrorMsg "prestop_failed, {{ .Values.proc_name }} - {{ $bus_addr }} run prestop failed." | tee -a $LAST_RUNCMD_LOGFILE
        exit 1
    fi

    PRESTOP_WAIT_TIME=1000
    WaitForMS $PRESTOP_WAIT_TIME
    custom_cmd=$(atapp_run_wrapper "$SCRIPT_DIR/{{ .Values.proc_name }}" -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml run prestop_check | grep "Custom Command")
    if echo "$custom_cmd" | grep -q "server prestop success"; then
        NoticeMsg "prestop_succeed, prestop {{ .Values.proc_name }} - {{ $bus_addr }} done." | tee -a $LAST_RUNCMD_LOGFILE
    elif echo "$custom_cmd" | grep -q "logic_server_common_module destroyed"; then
        NoticeMsg "prestop_succeed, prestop {{ .Values.proc_name }} - {{ $bus_addr }} done." | tee -a $LAST_RUNCMD_LOGFILE
    elif echo "$custom_cmd" | grep -q "server prestop not success yet"; then
        NoticeMsg "prestop_try_again, prestopping {{ .Values.proc_name }} - {{ $bus_addr }}, wait for a while." | tee -a $LAST_RUNCMD_LOGFILE
    else
        ErrorMsg "prestop_failed, prestop {{ .Values.proc_name }} - {{ $bus_addr }} return unknown, try again later." | tee -a $LAST_RUNCMD_LOGFILE
        exit 2
    fi
else
    NoticeMsg "prestop_succeed, send prestop command to {{ .Values.proc_name }} - {{ $bus_addr }} failed, not running." | tee $LAST_RUNCMD_LOGFILE
fi
{{- end }}
