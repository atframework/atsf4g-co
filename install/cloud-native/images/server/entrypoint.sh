#!/bin/bash

COMMAND=""
TIMEOUT=180
RUNCMD_PARAM=""

ENVS=()
PROJECT_ROOT_DIR="/data/projecty"
WORKDIR="${PROJECT_ROOT_DIR}/${SERVER_TYPE_NAME}/bin"
PID_FILE="${SERVER_TYPE_NAME}.pid"
INSTANCE_ENV_FILE="/opt/projecty/${SERVER_TYPE_NAME}/instance_env"

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

# if server running, it will return 0
function check_server_running() {
  local SEVER_PID_FILE=$1
  local SEVER_PIDS=($(ps -efH|grep "${SERVER_TYPE_NAME}"|grep "start"|grep -v grep|awk '{print $2}'))

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
  local SEVER_PIDS=($(ps -efH|grep "${SERVER_TYPE_NAME}"|grep "start"|grep -v grep|awk '{print $2}'))

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
  if [[ ! -d ${ENV_FILE_PATH} ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] instance env file path(${ENV_FILE_PATH}) not exist!!!"
    return 1
  fi

  if [ -f ${ENV_FILE} ]; then
    source ${ENV_FILE}
    return $?
  fi

  # specify instance id
  if [[ -z "${SERVER_INSTANCE_ID}" ]]; then
    add_env SERVER_INSTANCE_ID "$(/data/projecty/atdtool/atdtool guid gen)"
  fi

  #  add dsa instance env
  if [[ ${#ENVS[@]} -ne 0 ]]; then
    echo "export ${ENVS[@]}" > ${ENV_FILE}
  fi

  source ${ENV_FILE}
  return $?
}

function init_server_config() {
  # init_env_from_file "${INSTANCE_ENV_FILE}"
  # if [[ $? -ne 0 ]]; then
  #   return $?
  # fi
  mkdir -p ${WORKDIR}/../cfg/
  cp -f /etc/projecty/${SERVER_TYPE_NAME}/cfg/${SERVER_TYPE_NAME}.yaml ${WORKDIR}/../cfg/${SERVER_TYPE_NAME}.yaml
  if [[ $? -ne 0 ]]; then
    return $?
  fi

  cp -f /etc/projecty/${SERVER_TYPE_NAME}/cfg/vector.yaml ${WORKDIR}/../cfg/vector.yaml
  if [[ $? -ne 0 ]]; then
    return $?
  fi
  # init server config
  # envsubst < /etc/projecty/${SERVER_TYPE_NAME}/cfg/${SERVER_TYPE_NAME}.yaml > ${WORKDIR}/../cfg/${SERVER_TYPE_NAME}.yaml
  # if [[ $? -ne 0 ]]; then
  #   return $?
  # fi

  # # init server config
  # envsubst < /etc/projecty/${SERVER_TYPE_NAME}/cfg/vector.yaml > ${WORKDIR}/../cfg/vector.yaml
  # if [[ $? -ne 0 ]]; then
  #   return $?
  # fi

  return 0
}

parse_param "$@"

if [[ ! -d "${WORKDIR}" ]]; then
  echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] server workspace(${WORKDIR}) not exist!!!"
  exit 1
fi

# enter workspace
cd ${WORKDIR}

# prepare dynamic libary
if [[ -e "${PROJECT_ROOT_DIR}/tools/script/prepare-dependency-dll.sh" ]] && [[ -e "${WORKDIR}/package-version.txt" ]]; then
  CURRENT_PREPARE_PACKAGE_SHOR_SHA="$(cat "${WORKDIR}/package-version.txt" | grep vcs_short_sha | awk '{print $NF}')"
  find "${PROJECT_ROOT_DIR}/tools/script" -mindepth 1 -maxdepth 1 -name "prepare-package.*.lock" | grep -v -F "${CURRENT_PREPARE_PACKAGE_SHOR_SHA}" | xargs -r rm -f
  flock -x -w 20 "${PROJECT_ROOT_DIR}/tools/script/prepare-package.${CURRENT_PREPARE_PACKAGE_SHOR_SHA}.lock" bash "${PROJECT_ROOT_DIR}/tools/script/prepare-dependency-dll.sh" "${PROJECT_ROOT_DIR}" "${CURRENT_PREPARE_PACKAGE_SHOR_SHA}"
fi

if [[ "${COMMAND}" == "start" ]]; then
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
  ${WORKDIR}/${SERVER_TYPE_NAME}d -pid "${PID_FILE}" -config ../cfg/${SERVER_TYPE_NAME}.yaml -crash-output-file "../log/${SERVER_TYPE_NAME}_${ATAPP_INSTANCE_ID}.crash.log" start &
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

  chmod 755 /data/projecty/atdtool/atdtool
  # continuously observe configuration changes
  /data/projecty/atdtool/atdtool watch configmap "/etc/projecty/${SERVER_TYPE_NAME}" --command "/entrypoint.sh" --args "reload"
elif [[ "${COMMAND}" == "stop" ]]; then
  check_server_running ${PID_FILE}
  if [ $? -ne 0 ]; then
    stop_prepare_server
    exit 0
  fi

  BEGIN_TIME=$(date '+%s')
  END_TIME=$(($BEGIN_TIME+$TIMEOUT))
  echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] received stop command, timeout seconds(${TIMEOUT})"

  # prestop
  ${WORKDIR}/${SERVER_TYPE_NAME}d -p "${PID_FILE}" -c ../cfg/${SERVER_TYPE_NAME}.yaml run prestop
  if [[ $? -eq 0 ]]; then
    echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] run server prestop command success"
    while [[ $(date '+%s') -lt $END_TIME ]]; do
        sleep 1
        PRESTOP_STATUS=$(${WORKDIR}/${SERVER_TYPE_NAME}d -p "${PID_FILE}" -c ../cfg/${SERVER_TYPE_NAME}.yaml run prestop_check | grep "server prestop success" | wc -l)
        if [[ "${PRESTOP_STATUS}" -ge 1 ]]; then
            echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] prestop server done"
            break
        fi
    done
  else
    echo "$(date "+%Y/%m/%d %H:%M:%S") [ERROR] run prestop command failed!!!"
  fi

  # stop server
  ${WORKDIR}/${SERVER_TYPE_NAME}d -p "${PID_FILE}" -c ../cfg/${SERVER_TYPE_NAME}.yaml stop

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
  ${WORKDIR}/${SERVER_TYPE_NAME}d -p "${PID_FILE}" -c ../cfg/${SERVER_TYPE_NAME}.yaml reload
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
  ${WORKDIR}/${SERVER_TYPE_NAME}d -p "${PID_FILE}" -c ../cfg/${SERVER_TYPE_NAME}.yaml run ${RUNCMD_PARAM}
  echo "$(date "+%Y/%m/%d %H:%M:%S") [INFO] server runcmd done, ret[$?]"
elif [[ "${COMMAND}" == "health_check" ]]; then
  check_server_running ${PID_FILE}
  exit $?
else
  echo "[ERROR] unsupport command(${COMMAND})!!!"
  exit 1
fi
