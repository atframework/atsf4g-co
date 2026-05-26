{{- define "atapp.start.run.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}

SYSTEMD_SERVICE_FILE="{{ .Values.atapp.deployment.deployment_environment }}-{{ include "libapp.name" . }}-{{ $bus_addr }}.service"
echo "
[Unit]
Description={{ .Values.atapp.deployment.deployment_environment }}-{{ include "libapp.name" . }}-{{ $bus_addr }}
Wants=network-online.target
After=network-online.target

[Service]
Restart=on-failure
TimeoutStopSec=30
ExecStart=/bin/bash $SCRIPT_DIR/start.sh_{{ $bus_addr }}
ExecStop=/bin/bash $SCRIPT_DIR/stop.sh_{{ $bus_addr }}
PIDFile=$SCRIPT_DIR/$SERVER_PID_FILE_NAME
Type=simple

[Install]
WantedBy=default.target
" > "$SCRIPT_DIR/$SYSTEMD_SERVICE_FILE"
if [[ 0 -ne $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] ; then
    if [[ ! -e "$SERVER_PID_FILE_NAME" ]]; then
        echo 0 > "$SERVER_PID_FILE_NAME"
    fi

    SERVER_STARTUP_ERROR_FILE_NAME="${SERVER_PID_FILE_NAME/.pid/}.startup-error"
    if [ -e "$SERVER_STARTUP_ERROR_FILE_NAME" ]; then
        rm -f "$SERVER_STARTUP_ERROR_FILE_NAME"
    fi

    {{ include "libapp.run.wrapper.prehook.sh" (merge . (dict "enable_sanitizer" true "enable_hook_malloc" true)) | nindent 4 }}

    "$SCRIPT_DIR/{{ .Values.proc_name }}" -env {{ .Values.atapp.deployment.deployment_environment }} -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml start "$@" &

    RUNNING_EXE_CODE=$? ;
    if [[ "x${MSYSTEM}" != "x" ]]; then
        RUNNING_EXE_PID=$(ps -W | grep "^[[:space:]]*$!" | awk '{print $4}')
    else
        RUNNING_EXE_PID=$! ;
    fi
    {{ include "libapp.run.wrapper.posthook.sh" . | nindent 4 }}

    if [[ $RUNNING_EXE_CODE -ne 0 ]]; then
        ErrorMsg "[$(date -u '+%F %T')]: start {{ .Values.proc_name }} - {{ $bus_addr }} failed: $RUNNING_EXE_CODE .";
        exit 1;
    fi

    WaitProcessStarted "$SERVER_PID_FILE_NAME" 20000 $RUNNING_EXE_PID "$SERVER_STARTUP_ERROR_FILE_NAME"
    if [[ $? -ne 0 ]]; then
        ErrorMsg "[$(date -u '+%F %T')]: start {{ .Values.proc_name }} - {{ $bus_addr }} failed.";
        exit 1;
    fi

    NoticeMsg "[$(date -u '+%F %T')]: start {{ .Values.proc_name }} - {{ $bus_addr }} done.";
else
    NoticeMsg "[$(date -u '+%F %T')]: {{ .Values.proc_name }} - {{ $bus_addr }} already started, skipped.";
fi
{{- end }}
