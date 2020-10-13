set(GENERATE_FOR_PB_PY "${CMAKE_CURRENT_LIST_DIR}/generate-for-pb.py")
set(GENERATE_FOR_PB_WORK_DIR ${CMAKE_CURRENT_LIST_DIR})
set(GENERATE_FOR_PB_OUT_SH "${CMAKE_BINARY_DIR}/generate-for-pb-run.sh")
set(GENERATE_FOR_PB_OUT_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-run.ps1")
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
)

file(GLOB_RECURSE GENERATE_FOR_PB_PY_PROTO_FILES 
    "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config/*.proto"
    "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc/*.proto"
    "${ATFRAMEWORK_ATBUS_INC_DIR}/*.proto"
    "${ATFRAMEWORK_ATAPP_INC_DIR}/atframe/*.proto"
)
execute_process(
    COMMAND 
        ${GENERATE_FOR_PB_PROROC_BIN} "--proto_path" "${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension"
        "--proto_path" "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config"
        "--proto_path" "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc"
        "--proto_path" ${PROTOBUF_INCLUDE_DIRS}
        "--proto_path" ${ATFRAMEWORK_ATBUS_INC_DIR}
        "--proto_path" ${ATFRAMEWORK_ATAPP_INC_DIR}
        "-o" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
        "${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto"
        ${GENERATE_FOR_PB_PY_PROTO_FILES}
    RESULT_VARIABLE GENERATE_FOR_PB_PROTOC_RESULT
    WORKING_DIRECTORY ${GENERATE_FOR_PB_WORK_DIR}
    ${GENERATE_FOR_PB_PY_ENCODING} 
)
if (NOT GENERATE_FOR_PB_PROTOC_RESULT EQUAL 0)
    message(FATAL_ERROR "Run protoc failed.")
endif ()
unset(GENERATE_FOR_PB_PY_PROTO_FILES)
unset(GENERATE_FOR_PB_PROTOC_RESULT)

function(generate_for_pb_add_service SERVICE_NAME SERVICE_ROOT_DIR)
    set(GENERATE_FOR_PB_ARGS_OPTIONS "")
    set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE 
        PROTOBUF_PYTHON_PATH MAKO_PYTHON_PATH SIX_PYTHON_PATH)
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
    set(GENERATE_FOR_PB_ARGS_BASH_ARGS "--add-package-prefix \"${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}\"")
    set(GENERATE_FOR_PB_ARGS_CMAKE_ARGS "--add-package-prefix" ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR})
    if (GENERATE_FOR_PB_ARGS_PROTOBUF_PYTHON_PATH)
        set(GENERATE_FOR_PB_ARGS_BASH_ARGS " --add-path \"${GENERATE_FOR_PB_ARGS_PROTOBUF_PYTHON_PATH}\"")
        list(APPEND GENERATE_FOR_PB_ARGS_CMAKE_ARGS
            "--add-path" ${GENERATE_FOR_PB_ARGS_PROTOBUF_PYTHON_PATH}
        )
    endif ()
    if (GENERATE_FOR_PB_ARGS_MAKO_PYTHON_PATH)
        set(GENERATE_FOR_PB_ARGS_BASH_ARGS "${GENERATE_FOR_PB_ARGS_BASH_ARGS} --add-path \"${GENERATE_FOR_PB_ARGS_MAKO_PYTHON_PATH}\"")
        list(APPEND GENERATE_FOR_PB_ARGS_CMAKE_ARGS
            "--add-path" ${GENERATE_FOR_PB_ARGS_MAKO_PYTHON_PATH}
        )
    endif ()
    if (GENERATE_FOR_PB_ARGS_SIX_PYTHON_PATH)
        set(GENERATE_FOR_PB_ARGS_BASH_ARGS "${GENERATE_FOR_PB_ARGS_BASH_ARGS} --add-path \"${GENERATE_FOR_PB_ARGS_SIX_PYTHON_PATH}\"")
        list(APPEND GENERATE_FOR_PB_ARGS_CMAKE_ARGS
            "--add-path" ${GENERATE_FOR_PB_ARGS_SIX_PYTHON_PATH}
        )
    endif ()
    # Add bash script into generate-for-pb-run.sh
    file(APPEND ${GENERATE_FOR_PB_OUT_SH} "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}# ${SERVICE_NAME}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    file(APPEND ${GENERATE_FOR_PB_OUT_SH} 
        "\"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${PROJECT_SERVER_FRAME_BAS_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE} --set rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  ${GENERATE_FOR_PB_ARGS_BASH_ARGS} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.h.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h' \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.cpp.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_SH} "\"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE} --set rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  ${GENERATE_FOR_PB_ARGS_BASH_ARGS} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.h.mako:${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.h' \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.cpp.mako:${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_SH} "\"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --no-overwrite --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE} --set rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  ${GENERATE_FOR_PB_ARGS_BASH_ARGS} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.h.mako:${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h' \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.cpp.mako:${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    # Add powershell script into generate-for-pb-run.ps1
    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}# ${SERVICE_NAME}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} 
        "& \"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${PROJECT_SERVER_FRAME_BAS_DIR}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE} --set rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  ${GENERATE_FOR_PB_ARGS_BASH_ARGS} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.h.mako:rpc/\${service.get_extension_field(\\\"service_options\\\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h' `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.cpp.mako:rpc/\${service.get_extension_field(\\\"service_options\\\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "& \"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE} --set rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  ${GENERATE_FOR_PB_ARGS_BASH_ARGS} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.h.mako:${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.h' `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.cpp.mako:${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "& \"${Python3_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --no-overwrite --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE} --set rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  ${GENERATE_FOR_PB_ARGS_BASH_ARGS} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.h.mako:${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\\\"rpc_options\\\", lambda x: x.module_name, \\\"action\\\")}/task_action_\${rpc.get_name()}.h' `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.cpp.mako:${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\\\"rpc_options\\\", lambda x: x.module_name, \\\"action\\\")}/task_action_\${rpc.get_name()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    # Run script once when run cmake
    EchoWithColor(COLOR GREEN "-- Ready to run ${Python3_EXECUTABLE} ${GENERATE_FOR_PB_PY} for ${SERVICE_NAME} @ ${SERVICE_ROOT_DIR}")
    execute_process(
        COMMAND
            ${Python3_EXECUTABLE} ${GENERATE_FOR_PB_PY} "--quiet" "-s" "${SERVICE_NAME}" "-o" ${PROJECT_SERVER_FRAME_BAS_DIR}
            "--project-dir" ${PROJECT_SOURCE_DIR} "--pb-file" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
            "--set" "project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}" "--set" "rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}"
            "--add-path" "${ATFRAME_UTILS_ROOT}/project/cmake/modules"
            ${GENERATE_FOR_PB_ARGS_CMAKE_ARGS}
            "--service-template" "templates/rpc_call_api_for_ss.h.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h"
            "--service-template" "templates/rpc_call_api_for_ss.cpp.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp"
        COMMAND
            ${Python3_EXECUTABLE} ${GENERATE_FOR_PB_PY} "--quiet" "-s" ${SERVICE_NAME} "-o" ${SERVICE_ROOT_DIR}
            "--project-dir" ${PROJECT_SOURCE_DIR} "--pb-file" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
            "--set" "project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}" "--set" "rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}"
            "--add-path" "${ATFRAME_UTILS_ROOT}/project/cmake/modules"
            ${GENERATE_FOR_PB_ARGS_CMAKE_ARGS}
            "--service-template" "templates/handle_ss_rpc.h.mako:${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.h"
            "--service-template" "templates/handle_ss_rpc.cpp.mako:${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.cpp"
        COMMAND
            ${Python3_EXECUTABLE} ${GENERATE_FOR_PB_PY} "--quiet" "-s" ${SERVICE_NAME} "-o" ${SERVICE_ROOT_DIR}
            "--project-dir" ${PROJECT_SOURCE_DIR} "--no-overwrite" "--pb-file" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
            "--set" "project_namespace=${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}" "--set" "rpc_include_prefix=${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}"
            "--add-path" "${ATFRAME_UTILS_ROOT}/project/cmake/modules"
            ${GENERATE_FOR_PB_ARGS_CMAKE_ARGS}
            "--rpc-template" "templates/task_action_ss_rpc.h.mako:${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h"
            "--rpc-template" "templates/task_action_ss_rpc.cpp.mako:${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp"
        WORKING_DIRECTORY ${GENERATE_FOR_PB_WORK_DIR}
        ${GENERATE_FOR_PB_PY_ENCODING} 
    )
endfunction(generate_for_pb_add_service)
