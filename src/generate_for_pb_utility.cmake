set(GENERATE_FOR_PB_PY "${CMAKE_CURRENT_LIST_DIR}/generate-for-pb.py")
set(GENERATE_FOR_PB_WORK_DIR ${CMAKE_CURRENT_LIST_DIR})
set(GENERATE_FOR_PB_OUT_SH "${CMAKE_BINARY_DIR}/generate-for-pb-run.sh")
set(GENERATE_FOR_PB_OUT_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-run.ps1")
set(GENERATE_FOR_PB_OUT_CONF "${CMAKE_BINARY_DIR}/generate-for-pb-run.yaml")
if (NOT PROJECT_3RD_PARTY_PYTHON_MODULE_DIR)
    if (PROJECT_3RD_PARTY_INSTALL_DIR)
        set (PROJECT_3RD_PARTY_PYTHON_MODULE_DIR "${PROJECT_3RD_PARTY_INSTALL_DIR}/.python_modules")
    else ()
        set (PROJECT_3RD_PARTY_PYTHON_MODULE_DIR "${PROJECT_3RD_PARTY_ROOT_DIR}/.python_modules")
    endif ()
endif ()
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.11.0")
    set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF-8")
elseif (CMAKE_VERSION VERSION_GREATER_EQUAL "3.8.0")
    set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF8")
else ()
    unset(GENERATE_FOR_PB_PY_ENCODING)
endif ()
# Python3_EXECUTABLE
if (NOT Python3_EXECUTABLE)
    find_package (Python3 COMPONENTS Interpreter)
    if (NOT Python_Interpreter_FOUND)
        message(FATAL_ERROR "python3/python is required.")
    endif ()
endif()
if (Protobuf_PROTOC_EXECUTABLE)
    set(GENERATE_FOR_PB_PROROC_BIN ${Protobuf_PROTOC_EXECUTABLE})
elseif (PROTOBUF_PROTOC_EXECUTABLE)
    set(GENERATE_FOR_PB_PROROC_BIN ${PROTOBUF_PROTOC_EXECUTABLE})
elseif (TARGET protobuf::protoc)
    get_target_property(GENERATE_FOR_PB_PROROC_BIN protobuf::protoc IMPORTED_LOCATION)
    if (NOT GENERATE_FOR_PB_PROROC_BIN)
        get_target_property(GENERATE_FOR_PB_PROROC_BIN protobuf::protoc IMPORTED_LOCATION_RELEASE)
            if (NOT GENERATE_FOR_PB_PROROC_BIN)
            get_target_property(GENERATE_FOR_PB_PROROC_BIN protobuf::protoc IMPORTED_LOCATION_RELWITHDEBINFO)
        endif ()
        if (NOT GENERATE_FOR_PB_PROROC_BIN)
            get_target_property(GENERATE_FOR_PB_PROROC_BIN protobuf::protoc IMPORTED_LOCATION_MINSIZEREL)
        endif ()
        if (NOT GENERATE_FOR_PB_PROROC_BIN)
            get_target_property(GENERATE_FOR_PB_PROROC_BIN protobuf::protoc IMPORTED_LOCATION_DEBUG)
        endif ()
    endif ()
endif ()

if (NOT GENERATE_FOR_PB_PROROC_BIN)
    message(FATAL_ERROR "Protobuf - protoc is required.")
endif ()

file(WRITE ${GENERATE_FOR_PB_OUT_SH} "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(WRITE ${GENERATE_FOR_PB_OUT_PWSH} "#!/usr/bin/env pwsh${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "$OutputEncoding = [System.Text.UTF8Encoding]::new()${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

file(APPEND ${GENERATE_FOR_PB_OUT_SH} "cd \"${GENERATE_FOR_PB_WORK_DIR}\";${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "Set-Location \"${GENERATE_FOR_PB_WORK_DIR}\";${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

project_make_executable(${GENERATE_FOR_PB_OUT_SH})
project_make_executable(${GENERATE_FOR_PB_OUT_PWSH})

file(APPEND ${GENERATE_FOR_PB_OUT_SH}
    "\"${GENERATE_FOR_PB_PROROC_BIN}\" --proto_path \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROTOBUF_INCLUDE_DIRS}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_ATBUS_INC_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_ATAPP_INC_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  -o \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${ATFRAMEWORK_ATBUS_INC_DIR}\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${ATFRAMEWORK_ATAPP_INC_DIR}/atframe\"/*.proto ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "if [[ \$? -ne 0 ]]; then ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  echo \"Run protoc failed.\";${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  exit 1;${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "fi${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "\"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" --add-package-prefix \"${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}\" -c \"${GENERATE_FOR_PB_OUT_CONF}\""
)

file(APPEND ${GENERATE_FOR_PB_OUT_PWSH}
    "& \"${GENERATE_FOR_PB_PROROC_BIN}\" --proto_path \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROTOBUF_INCLUDE_DIRS}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_ATBUS_INC_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${ATFRAMEWORK_ATAPP_INC_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  -o \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \$(Get-ChildItem \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \$(Get-ChildItem \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \$(Get-ChildItem \"${ATFRAMEWORK_ATBUS_INC_DIR}/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \$(Get-ChildItem \"${ATFRAMEWORK_ATAPP_INC_DIR}/atframe/*.proto\") ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "if ( \$LASTEXITCODE -ne 0 ) { ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  Write-Error \"Run protoc failed\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  Exit 1${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "& \"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" --add-package-prefix \"${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}\" -c \"${GENERATE_FOR_PB_OUT_CONF}\""
)

file(WRITE ${GENERATE_FOR_PB_OUT_CONF} "configure:
  encoding: 'UTF-8'
  output_directory: 'output directory'
  overwrite: false
  paths: 
    - ${ATFRAME_UTILS_ROOT}/project/cmake/modules
  package_prefix:
    - ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}
  protocol_input_pb_file: '${PROJECT_INSTALL_RES_PBD_DIR}/network.pb'
  protocol_project_directory: '${PROJECT_SOURCE_DIR}'

rules:
")

function(generate_for_pb_add_service SERVICE_NAME SERVICE_ROOT_DIR)
    set(GENERATE_FOR_PB_ARGS_OPTIONS "")
    set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE)
    set(GENERATE_FOR_PB_ARGS_MULTI_VALUE "")
    cmake_parse_arguments(PARSE_ARGV 2 GENERATE_FOR_PB_ARGS 
        "${GENERATE_FOR_PB_ARGS_OPTIONS}" "${GENERATE_FOR_PB_ARGS_ONE_VALUE}" "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}")
    if (NOT GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX)
        set(GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX ".")
    endif ()
    if (GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX)
        set(HANDLE_PATH_PREFIX "${GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX}/")
    else ()
        set(HANDLE_PATH_PREFIX "")
    endif ()
    if (NOT GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE)
        set(GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE "")
    endif ()

    file(APPEND ${GENERATE_FOR_PB_OUT_CONF} "  # ${SERVICE_NAME}
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
      rpc_template:
        - overwrite: false
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/task_action_ss_rpc.h.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h'
        - overwrite: false
          input: '${GENERATE_FOR_PB_WORK_DIR}/templates/task_action_ss_rpc.cpp.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp'
")

    EchoWithColor(COLOR GREEN "-- Generate RPC configure for ${SERVICE_NAME} @ ${SERVICE_ROOT_DIR}")
endfunction(generate_for_pb_add_service)

function(generate_for_pb_run_generator)
    find_program (GENERATE_FOR_PB_POWERSHELL_BIN NAMES pwsh pwsh.exe)
    if (NOT GENERATE_FOR_PB_POWERSHELL_BIN)
        find_program (GENERATE_FOR_PB_POWERSHELL_BIN NAMES powershell powershell.exe)
    endif ()
    if (NOT GENERATE_FOR_PBF_POWERSHELL_BIN AND NOT (CMAKE_HOST_UNIX OR MSYS))
        EchoWithColor(COLOR RED "-- Dependency: powershell-core or powershell is required to configure rpc generator")
        message(FATAL_ERROR "powershell-core or powershell is required")
    endif ()

    if (GENERATE_FOR_PBF_POWERSHELL_BIN)
        execute_process(
            COMMAND ${GENERATE_FOR_PB_POWERSHELL_BIN} ${GENERATE_FOR_PB_OUT_PWSH}
            RESULT_VARIABLE GENERATE_FOR_PB_PROTOC_RESULT
            WORKING_DIRECTORY ${GENERATE_FOR_PB_WORK_DIR}
            ${GENERATE_FOR_PB_PY_ENCODING} 
        )
        if (NOT GENERATE_FOR_PB_PROTOC_RESULT EQUAL 0)
            message(FATAL_ERROR "Run ${GENERATE_FOR_PB_POWERSHELL_BIN} ${GENERATE_FOR_PB_OUT_PWSH} failed.")
        endif ()
    else ()
        execute_process(
            COMMAND bash ${GENERATE_FOR_PB_OUT_SH}
            RESULT_VARIABLE GENERATE_FOR_PB_PROTOC_RESULT
            WORKING_DIRECTORY ${GENERATE_FOR_PB_WORK_DIR}
            ${GENERATE_FOR_PB_PY_ENCODING} 
        )
        if (NOT GENERATE_FOR_PB_PROTOC_RESULT EQUAL 0)
            message(FATAL_ERROR "Run bash ${GENERATE_FOR_PB_OUT_SH} failed.")
        endif ()
    endif ()
endfunction()
