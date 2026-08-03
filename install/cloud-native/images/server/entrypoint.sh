#!/bin/bash

COMMAND=""
TIMEOUT=180
RUNCMD_PARAM=""

ENVS=()
PROJECT_ROOT_DIR="/data/atframework/publish"
WORKDIR="${PROJECT_ROOT_DIR}/${SERVER_TYPE_NAME}/bin"
PID_FILE="${WORKDIR}/${SERVER_TYPE_NAME}.pid"
INSTANCE_ENV_FILE="/opt/atframework/publish/${SERVER_TYPE_NAME}/instance_env"
ATDTOOL_BIN="${PROJECT_ROOT_DIR}/tools/atdtool/atdtool"

function usage() {
  cat <<EOF
Usage:
  $0 [start|stop|reload|kill|runcmd|health_check]

Flags:
    -h, --help            show help
    --timeout             specify operator timeout seconds
EOF
  exit 1
}

function parse_param() {
  if [[ $# -lt 1 ]];then
    eval set -- "-h"
  fi

  ARGS=`getopt -o h -l timeout:,args:,help -- "$@"`
  eval set -- "${ARGS}"
  while true; do
    case "$1" in
      -h|--help) usage ;;
      --timeout) TIMEOUT="$2" ; shift 2 ;;
      --) shift ; break ;;
      *) echo "Internal error!"; exit 1 ;;
    esac
  done

  COMMAND="$1"
  if [[ -z "${COMMAND}" ]]; then
    usage
  fi

  shift
  if [[ "${COMMAND}" == "runcmd" ]]; then
    RUNCMD_PARAM="$@"
  fi
}

function add_env() {
  if [[ $# -ne 2 ]]; then
    return 1
  fi

  ENVS=("${ENVS[@]}" "$1=\"$2\"")
}

function write_env_file() {
  local ENV_FILE="$1"
  local TMP_ENV_FILE="${ENV_FILE}.tmp"
  local ENV_ENTRY=""
  local RET=0

  : > "${TMP_ENV_FILE}"
  RET=$?
  if [[ ${RET} -ne 0 ]]; then
    return ${RET}
  fi

  for ENV_ENTRY in "${ENVS[@]}"; do
    printf 'export %s\n' "${ENV_ENTRY}" >> "${TMP_ENV_FILE}"
    RET=$?
    if [[ ${RET} -ne 0 ]]; then
      rm -f "${TMP_ENV_FILE}"
      return ${RET}
    fi
  done

  mv -f "${TMP_ENV_FILE}" "${ENV_FILE}"
  return $?
}

function normalize_env_file() {
  local ENV_FILE="$1"
  local TMP_ENV_FILE="${ENV_FILE}.tmp"

  awk '
    /^[[:space:]]*$/ {
      next
    }
    /^[[:space:]]*export[[:space:]]+[A-Za-z_][A-Za-z0-9_]*=.*/ {
      sub(/^[[:space:]]*/, "", $0)
      print
      next
    }
    /^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*=.*/ {
      sub(/^[[:space:]]*/, "", $0)
      print "export " $0
      next
    }
    {
      exit 1
    }
  ' "${ENV_FILE}" > "${TMP_ENV_FILE}"
  if [[ $? -ne 0 ]]; then
    rm -f "${TMP_ENV_FILE}"
    echo "$(date \"+%Y/%m/%d %H:%M:%S\") [ERROR] instance env file(${ENV_FILE}) only supports export KEY=value lines"
    return 1
  fi

  mv -f "${TMP_ENV_FILE}" "${ENV_FILE}"
  return $?
}

# if server running, it will return 0
function check_server_running() {
  local SEVER_PID_FILE=$1
  SEVER_PIDS=($(ps -efH|grep "${SERVER_TYPE_NAME}"|grep "start"|grep -v grep|awk '{print $2}'))

  if [[ ! -f ${SEVER_PID_FILE} ]]; then
    return 1
  fi

  local TARGET_PID=$(cat ${SEVER_PID_FILE})
  if [[ "x${TARGET_PID}" = "x" ]]; then
    return 1
  fi

  for pid in ${SEVER_PIDS[@]}; do
    if [[ "${pid}" = "${TARGET_PID}" ]]; then
      return 0
    fi
  done
  return 1
}

# force stop server
function kill_server() {
  local SEVER_PID_FILE=$1
  SEVER_PIDS=($(ps -efH|grep "${SERVER_TYPE_NAME}"|grep "start"|grep -v grep|awk '{print $2}'))

  if [[ ! -f ${SEVER_PID_FILE} ]]; then
    return 1
  fi

  local TARGET_PID=$(cat ${SEVER_PID_FILE})
  if [[ "x${TARGET_PID}" = "x" ]]; then
    return 1
  fi

  if [[ $# -gt 1 ]]; then
    kill -"$2" ${TARGET_PID}
  else
    kill ${TARGET_PID}
  fi
  return $?
}

# stop watch configmap
function stop_watch_configmap() {
  local TARGET_PID=$(ps -efH|grep atdtool|grep "watch configmap"|grep -v grep|awk '{print $2}')
  if [[ "x${TARGET_PID}" = "x" ]]; then
    return 0
  fi
  kill ${TARGET_PID}
  return $?
}

# stop prepare server
function stop_prepare_server() {
  local TARGET_PID=$(ps -efH|grep flock|grep -v grep|awk '{print $2}')
  if [[ "x${TARGET_PID}" = "x" ]]; then
    return 0
  fi
  kill -9 ${TARGET_PID}
  return $?
}

# init env from file
function init_env_from_file() {
  local ENV_FILE="$1"
  local ENV_FILE_PATH="${ENV_FILE%/*}"
  local RET=0
  if [[ ! -d ${ENV_FILE_PATH} ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] instance env file path(${ENV_FILE_PATH}) not exist!!!"
    return 1
  fi

  if [[ -f ${ENV_FILE} ]]; then
    normalize_env_file "${ENV_FILE}"
    RET=$?
    if [[ ${RET} -ne 0 ]]; then
      return ${RET}
    fi

    source "${ENV_FILE}"
    return $?
  fi

  # specify instance id
  if [[ -z "${SERVER_INSTANCE_ID}" ]]; then
    ordinal=${HOSTNAME##*-}
    echo $ordinal
    add_env SERVER_INSTANCE_ID $ordinal
    add_env ATAPP_INSTANCE_ID ${WORLD_ID}.${ZONE_ID}.${TYPE_ID}.${ordinal}
  fi

  #  add dsa instance env
  if [[ ${#ENVS[@]} -ne 0 ]]; then
    write_env_file "${ENV_FILE}"
    RET=$?
    if [[ ${RET} -ne 0 ]]; then
      return ${RET}
    fi
  fi

  normalize_env_file "${ENV_FILE}"
  RET=$?
  if [[ ${RET} -ne 0 ]]; then
    return ${RET}
  fi

  source "${ENV_FILE}"
  return $?
}

function init_external_ip() {
  if [[ -n "${ATAPP_EXTERNAL_IP}" ]]; then
    export ATAPP_EXTERNAL_IP
    return 0
  fi

  if [[ "${SERVER_TYPE_NAME}" == "atproxy" ]]; then
    if [[ -z "${ATAPP_RUNTIME_STATUS_POD_IP}" ]]; then
      echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] ATAPP_RUNTIME_STATUS_POD_IP is empty for atproxy"
      return 1
    fi

    export ATAPP_EXTERNAL_IP="${ATAPP_RUNTIME_STATUS_POD_IP}"
    return 0
  fi

  if [[ -z "${ATAPP_ATPROXY_SERVICE}" ]]; then
    return 0
  fi

  local ATAPP_ATPROXY_SERVICE_IP=""
  ATAPP_ATPROXY_SERVICE_IP="$(getent ahosts "${ATAPP_ATPROXY_SERVICE}" 2>/dev/null | awk '{print $1}' | awk '!seen[$0]++ {print; exit}')"
  if [[ -z "${ATAPP_ATPROXY_SERVICE_IP}" ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] resolve atproxy service(${ATAPP_ATPROXY_SERVICE}) failed"
    return 1
  fi

  export ATAPP_EXTERNAL_IP="${ATAPP_ATPROXY_SERVICE_IP}"
  export ATAPP_PROXY_EXTERNAL_IP="${ATAPP_ATPROXY_SERVICE_IP}"
  return 0
}

function init_server_config() {
  local RET=0

  init_env_from_file "${INSTANCE_ENV_FILE}"
  RET=$?
  if [[ ${RET} -ne 0 ]]; then
    return ${RET}
  fi

  init_external_ip
  RET=$?
  if [[ ${RET} -ne 0 ]]; then
    return ${RET}
  fi

  mkdir -p ${WORKDIR}/../cfg/
  # cp -f /etc/atframework/publish/${SERVER_TYPE_NAME}/cfg/*.yaml ${WORKDIR}/../cfg/
  # if [[ $? -ne 0 ]]; then
  #   return 1
  # fi

  envsubst < /etc/atframework/publish/${SERVER_TYPE_NAME}/cfg/${SERVER_TYPE_NAME}.yaml > ${WORKDIR}/../cfg/${SERVER_TYPE_NAME}.yaml
  RET=$?
  if [[ ${RET} -ne 0 ]]; then
    return ${RET}
  fi

  return 0
}

parse_param "$@"

if [[ ! -d "${WORKDIR}" ]]; then
  echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] server workspace(${WORKDIR}) not exist!!!"
  exit 1
fi

# enter workspace
cd ${WORKDIR}

if [[ "${COMMAND}" == "start" ]]; then
  # prepare dynamic libary (only needed on start, not health_check/stop/reload)
  if [[ -e "${PROJECT_ROOT_DIR}/tools/script/prepare-dependency-dll.sh" ]] && [[ -e "${WORKDIR}/package-version.txt" ]]; then
    CURRENT_PREPARE_PACKAGE_SHOR_SHA="$(cat "${WORKDIR}/package-version.txt" | grep vcs_short_sha | awk '{print $NF}')"
    find "${PROJECT_ROOT_DIR}/tools/script" -mindepth 1 -maxdepth 1 -name "prepare-package.*.lock" | grep -v -F "${CURRENT_PREPARE_PACKAGE_SHOR_SHA}" | xargs -r rm -f
    flock -x -w 20 "${PROJECT_ROOT_DIR}/tools/script/prepare-package.${CURRENT_PREPARE_PACKAGE_SHOR_SHA}.lock" bash "${PROJECT_ROOT_DIR}/tools/script/prepare-dependency-dll.sh" "${PROJECT_ROOT_DIR}" "${CURRENT_PREPARE_PACKAGE_SHOR_SHA}"
  fi
  check_server_running ${PID_FILE}
  if [[ $? -eq 0 ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] server already started!!!"
    exit 1
  fi

  init_server_config
  if [[ $? -ne 0 ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] init server configuration failed!!!"
    exit 1
  fi

  BEGIN_TIME=$(date '+%s')
  END_TIME=$(($BEGIN_TIME+$TIMEOUT))

  # start server
  ${WORKDIR}/${SERVER_TYPE_NAME}d --pid "${PID_FILE}" --config ../cfg/${SERVER_TYPE_NAME}.yaml --crash-output-file "../log/${SERVER_TYPE_NAME}_${ATAPP_INSTANCE_ID}.crash.log" -id "${ATAPP_INSTANCE_ID}" start &
  if [[ $? -ne 0 ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] server start failed!!!"
    exit 1
  fi

  # check server status again
  check_server_running ${PID_FILE}
  SERVER_STATUS=$?
  while [[ ${SERVER_STATUS} -ne 0 ]] && [[ $(date '+%s') -lt $END_TIME ]]; do
    echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] wait server status ready"
    sleep 1
    check_server_running ${PID_FILE}
    SERVER_STATUS=$?
  done

  if [[ ${SERVER_STATUS} -ne 0 ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] server start failed!!!"
    exit 1
  fi

  chmod 755 ${ATDTOOL_BIN}
  # continuously observe configuration changes
  ${ATDTOOL_BIN} watch configmap "/etc/atframework/publish/${SERVER_TYPE_NAME}" --command "/entrypoint.sh" --args "reload"
elif [[ "${COMMAND}" == "stop" ]]; then
  check_server_running ${PID_FILE}
  if [ $? -ne 0 ]; then
    stop_prepare_server
    exit 0
  fi

  BEGIN_TIME=$(date '+%s')
  END_TIME=$(($BEGIN_TIME+$TIMEOUT))
  echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] received stop command, timeout seconds(${TIMEOUT})"


  SEVER_PIDS=($(ps -efH|grep "${SERVER_TYPE_NAME}"|grep "start"|grep -v grep|awk '{print $2}'))

  # stop server
  kill ${SEVER_PIDS}

  # wait server stop finished
  check_server_running ${PID_FILE}
  while [ $? -eq 0 ]; do
    sleep 1
    check_server_running ${PID_FILE}
  done
  
  stop_watch_configmap
elif [[ "${COMMAND}" == "reload" ]]; then
  init_server_config
  if [[ $? -ne 0 ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] init server configuration failed!!!"
    exit 1
  fi
  
  # reload server
  SEVER_PIDS=($(ps -efH|grep "${SERVER_TYPE_NAME}"|grep "start"|grep -v grep|awk '{print $2}'))
  kill -SIGHUP ${SEVER_PIDS}

  echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] server reload done, ret[$?]"
elif [[ "${COMMAND}" == "kill" ]]; then
  echo "$(date "+%Y/%m/%d %H:%M:%S") [WARN] try force stop server"

  kill_server ${PID_FILE}
  if [[ $? -ne 0 ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] kill server failed!!!"
    exit $?
  fi

  BEGIN_TIME=$(date '+%s')
  END_TIME=$(($BEGIN_TIME+15))
  # wait server stop finished
  check_server_running ${PID_FILE}
  while [ $? -eq 0 ]; do
    if [[ $(date '+%s') -gt $END_TIME ]]; then
      echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] kill server timeout!!!"
      
      # force kill again
      kill_server ${PID_FILE} "SIGKILL"
      if [[ $? -ne 0 ]]; then
        echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] kill server failed!!!"
        exit $?
      fi
      break
    fi

    sleep 1
    check_server_running ${PID_FILE}
  done

  stop_watch_configmap
elif [[ "${COMMAND}" == "runcmd" ]]; then
  ${WORKDIR}/${SERVER_TYPE_NAME}d --pid "${PID_FILE}" --config ../cfg/${SERVER_TYPE_NAME}.yaml -id "${ATAPP_INSTANCE_ID}" run ${RUNCMD_PARAM}
  echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] server runcmd done, ret[$?]"
elif [[ "${COMMAND}" == "health_check" ]]; then
  check_server_running ${PID_FILE}
  exit $?
else
  echo "[ERROR] unsupport command(${COMMAND})!!!"
  exit 1
fi
