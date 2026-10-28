{{- define "atapp.start.run.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}

SERVER_STARTUP_ERROR_FILE_NAME="${SERVER_PID_FILE_NAME/.pid/}.startup-error"
if [[ $ATAPP_FOREGROUND -eq 1 ]]; then
    # The manager tracks the server itself, including after a crash restart.
    echo 0 > "$SERVER_PID_FILE_NAME"
    rm -f "$SERVER_STARTUP_ERROR_FILE_NAME"
    {{ include "libapp.run.wrapper.prehook.sh" (merge . (dict "enable_sanitizer" true "enable_hook_malloc" true)) | nindent 4 }}
    exec "$SCRIPT_DIR/{{ .Values.proc_name }}" -env {{ .Values.atapp.deployment.deployment_environment }} -id {{ $bus_addr }} -p "$SERVER_PID_FILE_NAME" --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml start "$@"
fi

bash "$SERVICE_MANAGER" start "$SERVICE_STATE_DIR" "{{ .Values.atapp.deployment.deployment_environment }}-{{ include "libapp.name" . }}-{{ $bus_addr }}" "$SCRIPT_DIR" -- /bin/bash "$SCRIPT_DIR/$(basename "$0")" --atapp-foreground "$@" || exit $?
RUNNING_EXE_PID=$(bash "$SERVICE_MANAGER" pid "$SERVICE_STATE_DIR")
WaitProcessStarted "$SERVER_PID_FILE_NAME" 20000 "${RUNNING_EXE_PID:-0}" "$SERVER_STARTUP_ERROR_FILE_NAME"
if [[ $? -ne 0 ]]; then
    bash "$SERVICE_MANAGER" stop "$SERVICE_STATE_DIR"
    ErrorMsg "[$(date -u '+%F %T')]: start {{ .Values.proc_name }} - {{ $bus_addr }} failed."
    exit 1
fi
NoticeMsg "[$(date -u '+%F %T')]: start {{ .Values.proc_name }} - {{ $bus_addr }} done."
{{- end }}
