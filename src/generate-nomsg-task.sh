#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(readlink -f "$SCRIPT_DIR/..")"

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <output path> <task name> [options of generate-for-pb.py]..."
  exit 1
fi

PROTOBUF_INCLUDE_DIR="$(find "$PROJECT_DIR/third_party/install" -name include -type d | head -n 1)"
PROTOBUF_BIN="$(find "$PROJECT_DIR/third_party/install" -name protoc -xtype f | head -n 1)"
EXTERNAL_MODULE_PATH="$(find "$PROJECT_DIR/third_party/install" -name .modules -xtype d | head -n 1)"

OUTPUT_DIR="$1"
TASK_NAME="$2"
shift
shift

if [[ -z "$PYTHON_BIN" ]]; then
  which python3 >/dev/null 2>&1
  if [[ $? -eq 0 ]]; then
    PYTHON_BIN=python3
  else
    which python >/dev/null 2>&1
    if [[ $? -eq 0 ]]; then
      PYTHON_BIN=python
    else
      echo "python/python3 is required."
      exit 1
    fi
  fi
fi

"$PYTHON_BIN" "$SCRIPT_DIR/generate-for-pb.py" -o "$OUTPUT_DIR" \
  --no-overwrite \
  --project-dir "$PROJECT_DIR" --pb-file "D:/workspace/github/atframework/atsf4g-co/build_jobs_cmake_tools/publish/resource/pbdesc/network.pb" \
  --set "project_namespace=hello" --set "task_class_name=$TASK_NAME" \
  --add-path "$PROJECT_DIR/atframework/cmake-toolset/modules" \
  --add-package-prefix "$EXTERNAL_MODULE_PATH" \
  --global-template "$SCRIPT_DIR/templates/task_action_no_msg.h.mako:$TASK_NAME.h" \
  --global-template "$SCRIPT_DIR/templates/task_action_no_msg.cpp.mako:$TASK_NAME.cpp"

PYTHON_EXIT_CODE=$?

if [[ $PYTHON_EXIT_CODE -ne 0 ]]; then
  echo -e "May be you can run \033[32;1m$PYTHON_BIN -m pip install -user $PROJECT_DIR/third_party/python_env/requirements.txt\033[0m first and then try again."
  exit $PYTHON_EXIT_CODE
fi
