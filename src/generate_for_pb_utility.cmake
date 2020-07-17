set(GENERATE_FOR_PB_PY "${CMAKE_CURRENT_LIST_DIR}/generate-for-pb.py")
set(GENERATE_FOR_PB_WORK_DIR ${CMAKE_CURRENT_LIST_DIR})
set(GENERATE_FOR_PB_OUT_SH "${CMAKE_BINARY_DIR}/generate-for-pb-run.sh")
set(GENERATE_FOR_PB_OUT_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-run.ps1")
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.11.0")
    set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF-8")
elseif (CMAKE_VERSION VERSION_GREATER_EQUAL "3.8.0")
    set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF8")
else ()
    unset(GENERATE_FOR_PB_PY_ENCODING)
endif ()
find_package (Python COMPONENTS Interpreter)
if (NOT Python_Interpreter_FOUND)
    message(FATAL_ERROR "python3/python is required.")
endif ()
# Python_EXECUTABLE

file(WRITE ${GENERATE_FOR_PB_OUT_SH} "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(WRITE ${GENERATE_FOR_PB_OUT_PWSH} "#!/usr/bin/env pwsh${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "$OutputEncoding = [System.Text.UTF8Encoding]::new()${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

file(APPEND ${GENERATE_FOR_PB_OUT_SH} "cd \"${GENERATE_FOR_PB_WORK_DIR}\";${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "Set-Location \"${GENERATE_FOR_PB_WORK_DIR}\";${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

file(APPEND ${GENERATE_FOR_PB_OUT_SH}
    "\"${PROTOBUF_PROTOC_EXECUTABLE}\" --proto_path \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROTOBUF_INCLUDE_DIRS}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  -o \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config\"/*.proto \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc\"/*.proto ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
)

file(APPEND ${GENERATE_FOR_PB_OUT_PWSH}
    "& \"${PROTOBUF_PROTOC_EXECUTABLE}\" --proto_path \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  --proto_path \"${PROTOBUF_INCLUDE_DIRS}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  -o \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \"${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \$(Get-ChildItem \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config/*.proto\") `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    "  \$(Get-ChildItem \"${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc/*.proto\") ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
)

file(GLOB_RECURSE GENERATE_FOR_PB_PY_PROTO_FILES 
    "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config/*.proto"
    "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc/*.proto"
)
execute_process(
    COMMAND 
        ${PROTOBUF_PROTOC_EXECUTABLE} "--proto_path" "${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension"
        "--proto_path" "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config"
        "--proto_path" "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc"
        "--proto_path" ${PROTOBUF_INCLUDE_DIRS}
        "-o" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
        "${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto"
        ${GENERATE_FOR_PB_PY_PROTO_FILES}
    WORKING_DIRECTORY ${GENERATE_FOR_PB_WORK_DIR}
    ${GENERATE_FOR_PB_PY_ENCODING} 
)
unset(GENERATE_FOR_PB_PY_PROTO_FILES)

function(generate_for_pb_add_service SERVICE_NAME SERVICE_ROOT_DIR TASK_PATH_PREFIX HANDLE_PATH_PREFIX)
    # Add bash script into generate-for-pb-run.sh
    file(APPEND ${GENERATE_FOR_PB_OUT_SH} "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}# ${SERVICE_NAME}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    file(APPEND ${GENERATE_FOR_PB_OUT_SH} 
        "\"${Python_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${PROJECT_SERVER_FRAME_BAS_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=hello --set rpc_include_prefix=${TASK_PATH_PREFIX} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${Protobuf_INCLUDE_DIRS}/../python\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.h.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h' \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.cpp.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_SH} "\"${Python_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=hello --set rpc_include_prefix=${TASK_PATH_PREFIX} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${Protobuf_INCLUDE_DIRS}/../python\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.h.mako:${HANDLE_PATH_PREFIX}/handle_ss_rpc_\${service.get_name_lower_rule()}.h' \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.cpp.mako:${HANDLE_PATH_PREFIX}/handle_ss_rpc_\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_SH} "\"${Python_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --no-overwrite --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=hello --set rpc_include_prefix=${TASK_PATH_PREFIX} \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${Protobuf_INCLUDE_DIRS}/../python\" \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.h.mako:${TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h' \\${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.cpp.mako:${TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    # Add powershell script into generate-for-pb-run.ps1
    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}# ${SERVICE_NAME}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} 
        "& \"${Python_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${PROJECT_SERVER_FRAME_BAS_DIR}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=hello --set rpc_include_prefix=${TASK_PATH_PREFIX} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${Protobuf_INCLUDE_DIRS}/../python\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.h.mako:rpc/\${service.get_extension_field(\\\"service_options\\\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h' `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/rpc_call_api_for_ss.cpp.mako:rpc/\${service.get_extension_field(\\\"service_options\\\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "& \"${Python_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=hello --set rpc_include_prefix=${TASK_PATH_PREFIX} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${Protobuf_INCLUDE_DIRS}/../python\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.h.mako:${HANDLE_PATH_PREFIX}/handle_ss_rpc_\${service.get_name_lower_rule()}.h' `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --service-template 'templates/handle_ss_rpc.cpp.mako:${HANDLE_PATH_PREFIX}/handle_ss_rpc_\${service.get_name_lower_rule()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    file(APPEND ${GENERATE_FOR_PB_OUT_PWSH} "& \"${Python_EXECUTABLE}\" \"${GENERATE_FOR_PB_PY}\" -s ${SERVICE_NAME} -o \"${SERVICE_ROOT_DIR}\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --project-dir \"${PROJECT_SOURCE_DIR}\" --no-overwrite --pb-file \"${PROJECT_INSTALL_RES_PBD_DIR}/network.pb\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --set project_namespace=hello --set rpc_include_prefix=${TASK_PATH_PREFIX} `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${ATFRAME_UTILS_ROOT}/project/cmake/modules\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --add-path \"${Protobuf_INCLUDE_DIRS}/../python\" `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.h.mako:${TASK_PATH_PREFIX}/\${rpc.get_extension_field(\\\"rpc_options\\\", lambda x: x.module_name, \\\"action\\\")}/task_action_\${rpc.get_name()}.h' `${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
        "  --rpc-template 'templates/task_action_ss_rpc.cpp.mako:${TASK_PATH_PREFIX}/\${rpc.get_extension_field(\\\"rpc_options\\\", lambda x: x.module_name, \\\"action\\\")}/task_action_\${rpc.get_name()}.cpp' ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
    )

    # Run script once when run cmake
    EchoWithColor(COLOR GREEN "-- Ready to run ${Python_EXECUTABLE} ${GENERATE_FOR_PB_PY} for ${SERVICE_NAME} @ ${SERVICE_ROOT_DIR}")
    execute_process(
        COMMAND
            ${Python_EXECUTABLE} ${GENERATE_FOR_PB_PY} "--quiet" "-s" "${SERVICE_NAME}" "-o" ${PROJECT_SERVER_FRAME_BAS_DIR}
            "--project-dir" ${PROJECT_SOURCE_DIR} "--pb-file" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
            "--set" "project_namespace=hello" "--set" "rpc_include_prefix=${TASK_PATH_PREFIX}"
            "--add-path" "${ATFRAME_UTILS_ROOT}/project/cmake/modules"
            "--add-path" "${Protobuf_INCLUDE_DIRS}/../python"
            "--service-template" "templates/rpc_call_api_for_ss.h.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h"
            "--service-template" "templates/rpc_call_api_for_ss.cpp.mako:rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp"
        COMMAND
            ${Python_EXECUTABLE} ${GENERATE_FOR_PB_PY} "--quiet" "-s" ${SERVICE_NAME} "-o" ${SERVICE_ROOT_DIR}
            "--project-dir" ${PROJECT_SOURCE_DIR} "--pb-file" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
            "--set" "project_namespace=hello" "--set" "rpc_include_prefix=${TASK_PATH_PREFIX}"
            "--add-path" "${ATFRAME_UTILS_ROOT}/project/cmake/modules"
            "--add-path" "${Protobuf_INCLUDE_DIRS}/../python"
            "--service-template" "templates/handle_ss_rpc.h.mako:${HANDLE_PATH_PREFIX}/handle_ss_rpc_\${service.get_name_lower_rule()}.h"
            "--service-template" "templates/handle_ss_rpc.cpp.mako:${HANDLE_PATH_PREFIX}/handle_ss_rpc_\${service.get_name_lower_rule()}.cpp"
        COMMAND
            ${Python_EXECUTABLE} ${GENERATE_FOR_PB_PY} "--quiet" "-s" ${SERVICE_NAME} "-o" ${SERVICE_ROOT_DIR}
            "--project-dir" ${PROJECT_SOURCE_DIR} "--no-overwrite" "--pb-file" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
            "--set" "project_namespace=hello" "--set" "rpc_include_prefix=${TASK_PATH_PREFIX}"
            "--add-path" "${ATFRAME_UTILS_ROOT}/project/cmake/modules"
            "--add-path" "${Protobuf_INCLUDE_DIRS}/../python"
            "--rpc-template" "templates/task_action_ss_rpc.h.mako:${TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h"
            "--rpc-template" "templates/task_action_ss_rpc.cpp.mako:${TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp"
        WORKING_DIRECTORY ${GENERATE_FOR_PB_WORK_DIR}
        ${GENERATE_FOR_PB_PY_ENCODING} 
    )
endfunction(generate_for_pb_add_service)
