set(GENERATE_FOR_PB_PY "${CMAKE_CURRENT_LIST_DIR}/generate-for-pb.py")
set(GENERATE_FOR_PB_WORK_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(GENERATE_FOR_PB_OUT_SH "${CMAKE_BINARY_DIR}/generate-for-pb-run.sh")
set(GENERATE_FOR_PB_OUT_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-run.ps1")
set(GENERATE_FOR_PB_PROTO_SH "${CMAKE_BINARY_DIR}/generate-for-pb-generate-pb.sh")
set(GENERATE_FOR_PB_PROTO_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-generate-pb.ps1")
set(GENERATE_FOR_PB_OUT_CONF "${CMAKE_BINARY_DIR}/generate-for-pb-run.yaml")
set(GENERATE_FOR_PB_OUT_LOG "${CMAKE_CURRENT_BINARY_DIR}/generate-for-pb-run.log")
set(GENERATE_FOR_PB_OUT_PB "${PROJECT_GENERATED_PBD_DIR}/network.pb")
unset(GENERATE_FOR_PB_PROTO_COMMAND)
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
  protocol_input_pb_file: '${GENERATE_FOR_PB_OUT_PB}'
  protocol_project_directory: '${PROJECT_SOURCE_DIR}'

rules:
  # Rules to generate rpc codes
")

macro(generate_for_pb_add_proto_path)
  foreach(PROTO_PATH ${ARGN})
    list(APPEND GENERATE_FOR_PB_PROTO_PATHS "${PROTO_PATH}")
  endforeach()
endmacro()

macro(generate_for_pb_add_proto_file)
  foreach(PROTO_FILE ${ARGN})
    list(APPEND GENERATE_FOR_PB_PROTO_FILES "${PROTO_FILE}")
  endforeach()
endmacro()

function(generate_for_pb_add_ss_service SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS RPC_IGNORE_EMPTY_REQUEST)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE RPC_ROOT_DIR
                                     SERVICE_DLLEXPORT_DECL RPC_DLLEXPORT_DECL)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE INCLUDE_HEADERS)
  cmake_parse_arguments(GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}" "${GENERATE_FOR_PB_ARGS_ONE_VALUE}"
                        "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}" ${ARGN})
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
  if(NOT GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR)
    set(GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR "${PROJECT_SERVER_FRAME_BAS_DIR}")
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
    string(REGEX REPLACE "[-\\.]" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL "${TARGET_NAME}")
    string(REGEX REPLACE "[\\\$\\\\/]" "" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(REPLACE "::" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                   "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}_API")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
    string(REGEX REPLACE "_API\\$" "" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}_RPC_API" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
  endif()
  if(GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS)
    set(CUSTOM_INCLUDE_HEADERS "include_headers:")
    foreach(INCLUDE_HEADERS ${GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS})
      set(CUSTOM_INCLUDE_HEADERS "${CUSTOM_INCLUDE_HEADERS}
          - '${INCLUDE_HEADERS}'")
    endforeach()
  else()
    set(CUSTOM_INCLUDE_HEADERS "include_headers: [ ]")
  endif()

  file(
    APPEND "${GENERATE_FOR_PB_OUT_CONF}"
    "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: true
      output_directory: '${GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
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
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
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
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE SERVICE_DLLEXPORT_DECL
                                     RPC_DLLEXPORT_DECL)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE INCLUDE_HEADERS)
  cmake_parse_arguments(GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}" "${GENERATE_FOR_PB_ARGS_ONE_VALUE}"
                        "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}" ${ARGN})
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
  if(NOT GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
    string(REGEX REPLACE "[-\\.]" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL "${TARGET_NAME}")
    string(REGEX REPLACE "[\\\$\\\\/]" "" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(REPLACE "::" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                   "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}_API")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
    string(REGEX REPLACE "_API\\$" "" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}_RPC_API" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
  endif()
  if(GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS)
    set(CUSTOM_INCLUDE_HEADERS "include_headers:")
    foreach(INCLUDE_HEADERS ${GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS})
      set(CUSTOM_INCLUDE_HEADERS "${CUSTOM_INCLUDE_HEADERS}
          - '${INCLUDE_HEADERS}'")
    endforeach()
  else()
    set(CUSTOM_INCLUDE_HEADERS "include_headers: [ ]")
  endif()

  file(
    APPEND "${GENERATE_FOR_PB_OUT_CONF}"
    "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
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
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
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
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE PROJECT_NAMESPACE SERVICE_DLLEXPORT_DECL RPC_DLLEXPORT_DECL)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE INCLUDE_HEADERS)
  cmake_parse_arguments(GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}" "${GENERATE_FOR_PB_ARGS_ONE_VALUE}"
                        "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}" ${ARGN})
  if(NOT GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE)
    set(GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE "")
  endif()
  if(GENERATE_FOR_PB_ARGS_RPC_IGNORE_EMPTY_REQUEST)
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ 'google.protobuf.Empty' ]")
  else()
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_ignore_request: [ ]")
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
    string(REGEX REPLACE "[-\\.]" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL "${TARGET_NAME}")
    string(REGEX REPLACE "[\\\$\\\\/]" "" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(REPLACE "::" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                   "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}_API")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
    string(REGEX REPLACE "_API\\$" "" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}_RPC_API" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
  endif()
  if(GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS)
    set(CUSTOM_INCLUDE_HEADERS "include_headers:")
    foreach(INCLUDE_HEADERS ${GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS})
      set(CUSTOM_INCLUDE_HEADERS "${CUSTOM_INCLUDE_HEADERS}
          - '${INCLUDE_HEADERS}'")
    endforeach()
  else()
    set(CUSTOM_INCLUDE_HEADERS "include_headers: [ ]")
  endif()

  file(
    APPEND "${GENERATE_FOR_PB_OUT_CONF}"
    "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
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

function(generate_for_pb_initialize_sh SCRIPT_PATH)
  set(optionArgs COMMAND_ECHO STOP_ON_ERROR)
  set(oneValueArgs SET_LOCATION)
  set(multiValueArgs "")
  cmake_parse_arguments(generate_for_pb_initialize_sh "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  file(WRITE "${SCRIPT_PATH}" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

  if(generate_for_pb_initialize_sh_STOP_ON_ERROR)
    file(APPEND "${SCRIPT_PATH}" "set -e ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()
  if(generate_for_pb_initialize_sh_COMMAND_ECHO)
    file(APPEND "${SCRIPT_PATH}" "set -x ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()
  if(generate_for_pb_initialize_sh_SET_LOCATION)
    file(APPEND "${SCRIPT_PATH}"
         "cd \"${generate_for_pb_initialize_sh_SET_LOCATION}\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()

  project_make_executable("${SCRIPT_PATH}")
endfunction()

function(generate_for_pb_initialize_pwsh SCRIPT_PATH)
  set(optionArgs COMMAND_ECHO STOP_ON_ERROR)
  set(oneValueArgs SET_LOCATION)
  set(multiValueArgs "")
  cmake_parse_arguments(generate_for_pb_initialize_pwsh "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  file(WRITE "${SCRIPT_PATH}" "#!/usr/bin/env pwsh${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  file(APPEND "${SCRIPT_PATH}"
       "$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  file(APPEND "${SCRIPT_PATH}"
       "$OutputEncoding = [System.Text.UTF8Encoding]::new()${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

  if(generate_for_pb_initialize_pwsh_STOP_ON_ERROR)
    file(APPEND "${SCRIPT_PATH}" "$ErrorActionPreference = \"Stop\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()
  if(generate_for_pb_initialize_pwsh_COMMAND_ECHO)
    file(APPEND "${SCRIPT_PATH}" "Set-PSDebug -Trace 1 ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()

  if(generate_for_pb_initialize_pwsh_SET_LOCATION)
    file(APPEND "${SCRIPT_PATH}"
         "Set-Location \"${generate_for_pb_initialize_pwsh_SET_LOCATION}\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()

  project_make_executable("${SCRIPT_PATH}")
endfunction()

function(generate_for_pb_run_generator)
  generate_for_pb_initialize_sh("${GENERATE_FOR_PB_OUT_SH}" STOP_ON_ERROR SET_LOCATION "${GENERATE_FOR_PB_WORK_DIR}")
  generate_for_pb_initialize_sh("${GENERATE_FOR_PB_PROTO_SH}" STOP_ON_ERROR SET_LOCATION "${GENERATE_FOR_PB_WORK_DIR}")
  generate_for_pb_initialize_pwsh("${GENERATE_FOR_PB_OUT_PWSH}" STOP_ON_ERROR SET_LOCATION
                                  "${GENERATE_FOR_PB_WORK_DIR}")
  generate_for_pb_initialize_pwsh("${GENERATE_FOR_PB_PROTO_PWSH}" STOP_ON_ERROR SET_LOCATION
                                  "${GENERATE_FOR_PB_WORK_DIR}")

  file(
    APPEND "${GENERATE_FOR_PB_PROTO_SH}"
    "\"${GENERATE_FOR_PB_PROROC_BIN}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  -o \"${GENERATE_FOR_PB_OUT_PB}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_PROTO_PATHS)
    file(APPEND "${GENERATE_FOR_PB_PROTO_SH}"
         "  --proto_path \"${PROTO_PATH}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endforeach()
  foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_PROTO_FILES)
    if(PROTO_PATH MATCHES "[\\*\\?]")
      string(REPLACE "*" "\"*\"" PROTO_PATTERN "${PROTO_PATH}")
      string(REPLACE "?" "\"?\"" PROTO_PATTERN "${PROTO_PATTERN}")
      file(APPEND "${GENERATE_FOR_PB_PROTO_SH}" "  \"${PROTO_PATTERN}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    else()
      file(APPEND "${GENERATE_FOR_PB_PROTO_SH}" "  \"${PROTO_PATH}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    endif()
  endforeach()
  file(
    APPEND "${GENERATE_FOR_PB_PROTO_SH}"
    "  \"${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/any.proto\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/empty.proto\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/duration.proto\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/timestamp.proto\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/descriptor.proto\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  )
  file(
    APPEND "${GENERATE_FOR_PB_PROTO_SH}"
    "\"${CMAKE_COMMAND}\" -E copy_if_different \"${GENERATE_FOR_PB_OUT_PB}\" \"${PROJECT_INSTALL_RES_PBD_DIR}\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  )

  if(ATFRAMEWORK_CMAKE_TOOLSET_BASH)
    file(
      APPEND "${GENERATE_FOR_PB_OUT_SH}"
      "\"${ATFRAMEWORK_CMAKE_TOOLSET_BASH}\" \"${GENERATE_FOR_PB_PROTO_SH}\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )
  else()
    file(APPEND "${GENERATE_FOR_PB_OUT_SH}"
         "bash \"${GENERATE_FOR_PB_PROTO_SH}\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()
  file(
    APPEND "${GENERATE_FOR_PB_OUT_SH}"
    "\"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" --add-package-prefix \"${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}\" -c \"${GENERATE_FOR_PB_OUT_CONF}\" \"$@\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  )

  file(
    APPEND "${GENERATE_FOR_PB_PROTO_PWSH}"
    "& \"${GENERATE_FOR_PB_PROROC_BIN}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  -o \"${GENERATE_FOR_PB_OUT_PB}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_PROTO_PATHS)
    file(APPEND "${GENERATE_FOR_PB_PROTO_PWSH}"
         "  --proto_path \"${PROTO_PATH}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endforeach()
  foreach(PROTO_PATH IN LISTS GENERATE_FOR_PB_PROTO_FILES)
    if(PROTO_PATH MATCHES "[\\*\\?]")
      file(APPEND "${GENERATE_FOR_PB_PROTO_PWSH}"
           "  \$(Get-ChildItem \"${PROTO_PATH}\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    else()
      file(APPEND "${GENERATE_FOR_PB_PROTO_PWSH}" "  \"${PROTO_PATH}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    endif()
  endforeach()
  file(
    APPEND "${GENERATE_FOR_PB_PROTO_PWSH}"
    "  \$(Get-ChildItem \"${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \$(Get-ChildItem \"${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/any.proto\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/empty.proto\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/duration.proto\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/timestamp.proto\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/descriptor.proto\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "
if ($LastExitCode -ne 0) {
  exit $LastExitCode
}
")
  file(
    APPEND "${GENERATE_FOR_PB_PROTO_PWSH}"
    "& \"${CMAKE_COMMAND}\" -E copy_if_different \"${GENERATE_FOR_PB_OUT_PB}\" \"${PROJECT_INSTALL_RES_PBD_DIR}\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  )

  if(ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
    file(
      APPEND "${GENERATE_FOR_PB_OUT_PWSH}"
      "& \"${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}\" -File \"${GENERATE_FOR_PB_PROTO_PWSH}\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )
  else()
    file(APPEND "${GENERATE_FOR_PB_OUT_PWSH}"
         "pwsh -File \"${GENERATE_FOR_PB_PROTO_PWSH}\" ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()

  file(
    APPEND "${GENERATE_FOR_PB_OUT_PWSH}"
    "
if ($LastExitCode -ne 0) {
  exit $LastExitCode
}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}
& \"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" --add-package-prefix \"${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}\" -c \"${GENERATE_FOR_PB_OUT_CONF}\" $args
if ($LastExitCode -ne 0) {
  exit $LastExitCode
}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

  if(ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
    set(GENERATE_FOR_PB_PROTO_COMMAND
        "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" "-File" "${GENERATE_FOR_PB_OUT_PWSH}"
        PARENT_SCOPE)
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
    set(GENERATE_FOR_PB_PROTO_COMMAND
        "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${GENERATE_FOR_PB_OUT_SH}"
        PARENT_SCOPE)
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
