{{- define "atapp.debug.sh" -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

if [ "x" == "x${DEBUG_BIN}" ]; then
    DEBUG_BIN=gdb ;
fi

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

PROC_PID=$(cat "$SERVER_PID_FILE_NAME" 2>/dev/null)

SERVER_STARTUP_ERROR_FILE_NAME="${SERVER_PID_FILE_NAME/.pid/}.startup-error"
if [ -e "$SERVER_STARTUP_ERROR_FILE_NAME" ]; then
  rm -f "$SERVER_STARTUP_ERROR_FILE_NAME"
fi

if [[  -e "$SCRIPT_DIR/package-version.txt" ]]; then
    SOLIB_SHARED_PATH="$(grep -i -F "shared_rpath:" "$SCRIPT_DIR/package-version.txt")"
    if [[ ! -z "$SOLIB_SHARED_PATH" ]]; then
        SOLIB_SHARED_PATH="$(echo "$SOLIB_SHARED_PATH" | awk 'BEGIN{FS=":"}{print $NF}' | xargs -r echo)"
    fi
    SOLIB_PRIVATE_PATH="$(grep -i -F "private_rpath:" "$SCRIPT_DIR/package-version.txt")"
    if [[ ! -z "$SOLIB_PRIVATE_PATH" ]]; then
        SOLIB_PRIVATE_PATH="$(echo "$SOLIB_PRIVATE_PATH" | awk 'BEGIN{FS=":"}{print $NF}' | xargs -r echo)"
    fi
fi

SOLIB_SEARCH_PATHS=""
if [[ ! -z "$SOLIB_PRIVATE_PATH" ]]; then
    SOLIB_SEARCH_PATHS="$PROJECT_INSTALL_DIR/$SOLIB_PRIVATE_PATH"
fi
if [[ ! -z "$SOLIB_SHARED_PATH" ]]; then
    if [[ -z "$SOLIB_SEARCH_PATHS" ]]; then
        SOLIB_SEARCH_PATHS="$PROJECT_INSTALL_DIR/$SOLIB_SHARED_PATH"
    else
        SOLIB_SEARCH_PATHS="$SOLIB_SEARCH_PATHS:$PROJECT_INSTALL_DIR/$SOLIB_SHARED_PATH"
    fi
fi

DEBUG_BIN_BASENAME="$(basename "$DEBUG_BIN")";
if [[ ! -z "$SOLIB_SEARCH_PATHS" ]]; then
    if [[ "${DEBUG_BIN_BASENAME:0:3}" == "gdb" ]]; then
        SOLIB_SEARCH_PATH_OPTIONS=("--init-eval-command=set solib-search-path $SOLIB_SEARCH_PATHS")
    else
        SOLIB_SEARCH_PATH_OPTIONS=("--one-line settings set target.exec-search-paths $SOLIB_SEARCH_PATHS")
    fi
else
    SOLIB_SEARCH_PATH_OPTIONS=()
fi

DEBUG_COREFILE_MODE=0
for INPUT_OPT in "$@"; do
    if [[ "$INPUT_OPT" == "-c" ]] || [[ "$INPUT_OPT" == "--core" ]] || [[ "${INPUT_OPT:0:3}" == "-c=" ]] || [[ "${INPUT_OPT:0:7}" == "--core=" ]]; then
        DEBUG_COREFILE_MODE=1
        break
    fi
done

if [[ 0 -eq $(CheckPidAndExePath "$SCRIPT_DIR/{{ .Values.proc_name }}" $PROC_PID) ]] && [[ 0 -eq $DEBUG_COREFILE_MODE ]] ; then
    $DEBUG_BIN -p $PROC_PID "${SOLIB_SEARCH_PATH_OPTIONS[@]}" "$@"
else

    if [[ "${DEBUG_BIN_BASENAME:0:3}" == "gdb" ]]; then
        if [[ 0 -eq $DEBUG_COREFILE_MODE ]]; then
            "$DEBUG_BIN" "${SOLIB_SEARCH_PATH_OPTIONS[@]}" "$@" --args "$SCRIPT_DIR/{{ .Values.proc_name }}" -env {{ .Values.atapp.deployment.deployment_environment }} -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml start
        else
            "$DEBUG_BIN" "${SOLIB_SEARCH_PATH_OPTIONS[@]}" "$SCRIPT_DIR/{{ .Values.proc_name }}" "$@"
        fi
    else
        # lldb
        if [[ 0 -eq $DEBUG_COREFILE_MODE ]]; then
            "$DEBUG_BIN" "${SOLIB_SEARCH_PATH_OPTIONS[@]}" "$@" "$SCRIPT_DIR/{{ .Values.proc_name }}" -- -env {{ .Values.atapp.deployment.deployment_environment }} -id {{ $bus_addr }} -p $SERVER_PID_FILE_NAME --startup-error-file "$SERVER_STARTUP_ERROR_FILE_NAME" -c ../cfg/{{ include "libapp.name" . }}_{{ $bus_addr }}.yaml start
        else
            "$DEBUG_BIN" "${SOLIB_SEARCH_PATH_OPTIONS[@]}" "$SCRIPT_DIR/{{ .Values.proc_name }}" "$@"
        fi
    fi
fi
{{- end }}
