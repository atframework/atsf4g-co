set(GENERATE_FOR_PB_PY "${CMAKE_CURRENT_LIST_DIR}/generate-for-pb.py")
set(GENERATE_FOR_PB_WORK_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(GENERATE_FOR_PB_OUT_SH "${CMAKE_BINARY_DIR}/generate-for-pb-run.sh")
set(GENERATE_FOR_PB_OUT_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-run.ps1")
set(GENERATE_FOR_PB_OUT_CONF "${CMAKE_BINARY_DIR}/generate-for-pb-run.yaml")
set(GENERATE_FOR_PB_OUT_LOG "${CMAKE_CURRENT_BINARY_DIR}/generate-for-pb-run.log")
file(WRITE "${GENERATE_FOR_PB_OUT_LOG}" "# generate-for-pb-run")
if(NOT PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR)
  set(PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR "${PROJECT_THIRD_PARTY_INSTALL_DIR }/.python_modules")
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.11.0")
  set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF-8")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL "3.8.0")
  set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF8")
else()
  unset(GENERATE_FOR_PB_PY_ENCODING)
endif()
# Python3_EXECUTABLE
if(NOT Python3_EXECUTABLE)
  find_package(Python3 COMPONENTS Interpreter)
  if(NOT Python_Interpreter_FOUND)
    message(FATAL_ERROR "python3/python is required.")
  endif()
endif()

set(GENERATE_FOR_PB_PROROC_BIN ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC})
if(NOT GENERATE_FOR_PB_PROROC_BIN)
  message(FATAL_ERROR "Protobuf - protoc is required.")
endif()

file(WRITE ${GENERATE_FOR_PB_OUT_SH} "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_SH} "set -xe${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(WRITE ${GENERATE_FOR_PB_OUT_PWSH} "#!/usr/bin/env pwsh${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "$ErrorActionPreference = \"Stop\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH}
     "$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH}
     "$OutputEncoding = [System.Text.UTF8Encoding]::new()${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

file(APPEND ${GENERATE_FOR_PB_OUT_SH} "cd \"${GENERATE_FOR_PB_WORK_DIR}\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH}
     "Set-Location \"${GENERATE_FOR_PB_WORK_DIR}\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

project_make_executable(${GENERATE_FOR_PB_OUT_SH})
project_make_executable(${GENERATE_FOR_PB_OUT_PWSH})

file(APPEND ${GENERATE_FOR_PB_OUT_SH}
     "\"${GENERATE_FOR_PB_PROROC_BIN}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
     "  -o \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_PROTO_PATH)
  file(APPEND ${GENERATE_FOR_PB_OUT_SH} "  --proto_path \"${PROTO_PATH}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
endforeach()
foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_EXTEND_PROTO_FILES)
  file(APPEND ${GENERATE_FOR_PB_OUT_SH} "  \"${PROTO_PATH}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
endforeach()
file(
  APPEND ${GENERATE_FOR_PB_OUT_SH}
  "  \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  "  \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  "  \"${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  "  \"${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe\"/*.proto ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

file(
  APPEND ${GENERATE_FOR_PB_OUT_SH}
  "\"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -c \"${GENERATE_FOR_PB_OUT_CONF}\" \"$@\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
)

file(APPEND ${GENERATE_FOR_PB_OUT_PWSH}
     "& \"${GENERATE_FOR_PB_PROROC_BIN}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
     "  -o \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_PROTO_PATH)
  file(APPEND ${GENERATE_FOR_PB_OUT_PWSH}
       "  --proto_path \"${PROTO_PATH}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
endforeach()
foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_EXTEND_PROTO_FILES)
  file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "  \"${PROTO_PATH}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
endforeach()
file(
  APPEND ${GENERATE_FOR_PB_OUT_PWSH}
  "  \$(Get-ChildItem \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  "  \$(Get-ChildItem \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  "  \$(Get-ChildItem \"${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  "  \$(Get-ChildItem \"${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe/*.proto\") ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}

if ($LastExitCode -ne 0) {
  exit $LastExitCode
}
")

file(
  APPEND ${GENERATE_FOR_PB_OUT_PWSH}
  "
& \"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -c \"${GENERATE_FOR_PB_OUT_CONF}\" $args
if ($LastExitCode -ne 0) {
  exit $LastExitCode
}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

file(
  WRITE "${GENERATE_FOR_PB_OUT_CONF}"
  "configure:
  encoding: 'UTF-8'
  output_directory: 'output directory'
  overwrite: false
  paths:
    - \"${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/modules\"")
foreach(GENERATE_FOR_PB_ARGS_ADD_PATH GENERATE_FOR_PB_MODULE_PROTOBUF_PYTHON_PATH
                                      GENERATE_FOR_PB_MODULE_MAKO_PYTHON_PATH GENERATE_FOR_PB_MODULE_SIX_PYTHON_PATH)
  if(${GENERATE_FOR_PB_ARGS_ADD_PATH})
    file(APPEND "${GENERATE_FOR_PB_OUT_CONF}" "
      - \"${${GENERATE_FOR_PB_ARGS_ADD_PATH}}\"")
  endif()
endforeach()
file(
  APPEND "${GENERATE_FOR_PB_OUT_CONF}"
  "
  package_prefix:
    - \"${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}\"
  protocol_input_pb_file: '${PROJECT_INSTALL_RES_PBD_DIR}/network.pb'
  protocol_project_directory: '${PROJECT_SOURCE_DIR}'

rules:
  # Rules to generate rpc codes
")

function(generate_for_pb_add_ss_service SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS RPC_IGNORE_EMPTY_REQUEST)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE "")
  cmake_parse_arguments(PARSE_ARGV 2 GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}"
                        "${GENERATE_FOR_PB_ARGS_ONE_VALUE}" "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}")
  if(NOT GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX)
    set(GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX ".")
  endif()
  if(GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX)
    set(HANDLE_PATH_PREFIX "${GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX}/")
  else()
    set(HANDLE_PATH_PREFIX "")
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE)
    set(GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE "")
  endif()
  if(GENERATE_FOR_PB_ARGS_RPC_IGNORE_EMPTY_REQUEST)
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ 'google.protobuf.Empty' ]")
  else()
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ ]")
  endif()

  file(
    APPEND "${GENERATE_FOR_PB_OUT_CONF}"
    "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: true
      output_directory: '${PROJECT_SERVER_FRAME_BAS_DIR}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/rpc_call_api_for_ss.h.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/rpc_call_api_for_ss.cpp.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp'
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/handle_ss_rpc.h.mako'
          output: '${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/handle_ss_rpc.cpp.mako'
          output: '${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.cpp'
      ${GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST}
      rpc_template:
        - overwrite: false
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/task_action_ss_rpc.h.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h'
        - overwrite: false
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/task_action_ss_rpc.cpp.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp'
")
endfunction(generate_for_pb_add_ss_service)

function(generate_for_pb_add_cs_service SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS RPC_IGNORE_EMPTY_REQUEST)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE "")
  cmake_parse_arguments(PARSE_ARGV 2 GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}"
                        "${GENERATE_FOR_PB_ARGS_ONE_VALUE}" "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}")
  if(NOT GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX)
    set(GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX ".")
  endif()
  if(GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX)
    set(HANDLE_PATH_PREFIX "${GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX}/")
  else()
    set(HANDLE_PATH_PREFIX "")
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE)
    set(GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE "")
  endif()
  if(GENERATE_FOR_PB_ARGS_RPC_IGNORE_EMPTY_REQUEST)
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ 'google.protobuf.Empty' ]")
  else()
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ ]")
  endif()

  file(
    APPEND "${GENERATE_FOR_PB_OUT_CONF}"
    "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/session_downstream_api_for_cs.h.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/session_downstream_api_for_cs.cpp.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp'
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/handle_cs_rpc.h.mako'
          output: '${HANDLE_PATH_PREFIX}handle_cs_rpc_\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/handle_cs_rpc.cpp.mako'
          output: '${HANDLE_PATH_PREFIX}handle_cs_rpc_\${service.get_name_lower_rule()}.cpp'
      ${GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST}
      rpc_template:
        - overwrite: false
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/task_action_cs_rpc.h.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h'
        - overwrite: false
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/task_action_cs_rpc.cpp.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp'
")
endfunction(generate_for_pb_add_cs_service)

function(generate_for_pb_add_simulator_cs_api SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS RPC_IGNORE_EMPTY_REQUEST)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE PROJECT_NAMESPACE)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE "")
  cmake_parse_arguments(PARSE_ARGV 2 GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}"
                        "${GENERATE_FOR_PB_ARGS_ONE_VALUE}" "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}")
  if(NOT GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE)
    set(GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE "")
  endif()
  if(GENERATE_FOR_PB_ARGS_RPC_IGNORE_EMPTY_REQUEST)
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ 'google.protobuf.Empty' ]")
  else()
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ ]")
  endif()

  file(
    APPEND "${GENERATE_FOR_PB_OUT_CONF}"
    "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/package_request_api_for_simulator.h.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/package_request_api_for_simulator.cpp.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp'
      ${GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST}
")
endfunction(generate_for_pb_add_simulator_cs_api)

function(generate_for_pb_run_generator)
  if(ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
    execute_process(
      COMMAND "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" -File "${GENERATE_FOR_PB_OUT_PWSH}"
      RESULT_VARIABLE GENERATE_FOR_PB_PROTOC_RESULT
      WORKING_DIRECTORY "${GENERATE_FOR_PB_WORK_DIR}"
      OUTPUT_FILE "${GENERATE_FOR_PB_OUT_LOG}"
      ERROR_FILE "${GENERATE_FOR_PB_OUT_LOG}" COMMAND_ECHO STDOUT ${GENERATE_FOR_PB_PY_ENCODING})
    if(NOT GENERATE_FOR_PB_PROTOC_RESULT EQUAL 0)
      message(
        FATAL_ERROR
          "Run \"${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}\" \"${GENERATE_FOR_PB_OUT_PWSH}\" failed. See ${GENERATE_FOR_PB_OUT_LOG} for details."
      )
    endif()
  else()
    execute_process(
      COMMAND "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${GENERATE_FOR_PB_OUT_SH}"
      RESULT_VARIABLE GENERATE_FOR_PB_PROTOC_RESULT
      WORKING_DIRECTORY "${GENERATE_FOR_PB_WORK_DIR}"
      OUTPUT_FILE "${GENERATE_FOR_PB_OUT_LOG}"
      ERROR_FILE "${GENERATE_FOR_PB_OUT_LOG}" COMMAND_ECHO STDOUT ${GENERATE_FOR_PB_PY_ENCODING})
    if(NOT GENERATE_FOR_PB_PROTOC_RESULT EQUAL 0)
      message(
        FATAL_ERROR
          "Run \"${ATFRAMEWORK_CMAKE_TOOLSET_BASH}\" \"${GENERATE_FOR_PB_OUT_SH}\" failed. See ${GENERATE_FOR_PB_OUT_LOG} for details."
      )
    endif()
  endif()
endfunction()
