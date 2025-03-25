include("${CMAKE_CURRENT_LIST_DIR}/generate_proto_utility.cmake")

# ============= extension protocols =============
file(GLOB PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_EXTENSION
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/extension/*.proto"
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/extension/*.proto")
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}/protocol/extension"
  PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_EXTENSION ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_EXTENSION})
list(REMOVE_DUPLICATES PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_EXTENSION)

project_server_frame_create_protocol_target(
  protocol-extension
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}"
  PROJECT_SERVER_FRAME_PROTO_LIBRARY_EXTENSION
  DEPENDS
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_EXTENSION}
  PROTOCOLS
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_EXTENSION})

# ============= common protocols =============
file(GLOB PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/common/*.proto"
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/common/*.proto")
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}/protocol/common"
  PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
  # 需要和客户端同步，后续迁移到protocol-extension
  "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto")

# 需要和客户端同步，后续迁移到protocol-extension
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}/protocol/common/extensions/v3"
  PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto"
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader_ue.proto")

list(REMOVE_DUPLICATES PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON)

project_server_frame_create_protocol_target(
  protocol-common
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}"
  PROJECT_SERVER_FRAME_PROTO_LIBRARY_COMMON
  EXTERNAL_PROTO_PATH
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}
  DEPENDS
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_EXTENSION}
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
  PROTOCOLS
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON}
  PUBLIC_LINK_LIBRARIES
  ${PROJECT_SERVER_FRAME_PROTO_LIBRARY_EXTENSION}
  atbus-protocol
  atapp-protocol)

# ============= configure protocols =============
file(GLOB PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/config/*.proto"
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/config/*.proto")
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}/protocol/config" PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG} "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/pb_header_v3.proto")
list(REMOVE_DUPLICATES PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG)

project_server_frame_create_protocol_target(
  protocol-config
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
  PROJECT_SERVER_FRAME_PROTO_LIBRARY_CONFIG
  EXTERNAL_PROTO_PATH
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}"
  "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension"
  DEPENDS
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_EXTENSION}
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG}
  PROTOCOLS
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG}
  PUBLIC_LINK_LIBRARIES
  ${PROJECT_SERVER_FRAME_PROTO_LIBRARY_COMMON})

add_custom_command(
  OUTPUT "${PROJECT_GENERATED_PBD_DIR}/config.pb"
  COMMAND "${CMAKE_COMMAND}" -E remove -f "${PROJECT_GENERATED_PBD_DIR}/config.pb"
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path
    "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}" --proto_path
    "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}" --proto_path "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
    --proto_path "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}" --proto_path "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
    --proto_path "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include" -o "${PROJECT_GENERATED_PBD_DIR}/config.pb"
    # Protocol buffer files
    ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG} ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON}
    ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_EXTENSION}
    "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe/atapp_conf.proto"
    "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include/libatbus_protocol.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/any.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/empty.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/duration.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/timestamp.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/descriptor.proto"
  WORKING_DIRECTORY "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
  DEPENDS ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_EXTENSION} ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
          ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG}
          "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto"
  COMMENT "Generate [@${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}] ${PROJECT_GENERATED_PBD_DIR}/config.pb")

add_custom_command(
  OUTPUT "${PROJECT_INSTALL_RES_PBD_DIR}/config.pb"
  COMMAND "${CMAKE_COMMAND}" "-E" "copy_if_different" "${PROJECT_GENERATED_PBD_DIR}/config.pb"
          "${PROJECT_INSTALL_RES_PBD_DIR}"
  WORKING_DIRECTORY "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
  DEPENDS "${PROJECT_GENERATED_PBD_DIR}/config.pb"
  COMMENT "Generate [@${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}] ${PROJECT_INSTALL_RES_PBD_DIR}/config.pb")

# ============= Convert excel =============
find_package(Java REQUIRED COMPONENTS Runtime)
file(GLOB PROJECT_RESOURCE_EXCEL_FILES "${PROJECT_THIRD_PARTY_XRESLOADER_EXCEL_DIR}/*.xlsx")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_generated/xml")
configure_file("${PROJECT_SOURCE_DIR}/resource/excel_xml/xresconv.xml.in"
               "${CMAKE_CURRENT_BINARY_DIR}/_generated/xml/xresconv.gen.xml" ESCAPE_QUOTES @ONLY)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/_generated/xml/xresconv.gen.xml"
          "${CMAKE_CURRENT_BINARY_DIR}/_generated/xml/xresconv.xml" COMMAND_ECHO STDOUT)

set(PROJECT_RESOURCE_EXCEL_COMMAND_ARGS "\"${Python3_EXECUTABLE}\" \"${PROJECT_THIRD_PARTY_XRESLOADER_CLI}\"")
if(Java_JAVA_EXECUTABLE)
  set(PROJECT_RESOURCE_EXCEL_COMMAND_ARGS
      "${PROJECT_RESOURCE_EXCEL_COMMAND_ARGS} --java-path \"${Java_JAVA_EXECUTABLE}\"")
endif()
set(PROJECT_RESOURCE_EXCEL_COMMAND_ARGS
    "${PROJECT_RESOURCE_EXCEL_COMMAND_ARGS} \"${CMAKE_CURRENT_BINARY_DIR}/_generated/xml/xresconv.xml\"")

project_build_tool_generate_load_env_powershell("${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.sh"
     "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.sh" "set -ex${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.sh"
     "${PROJECT_RESOURCE_EXCEL_COMMAND_ARGS}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.sh"
     "\"${CMAKE_COMMAND}\" -E touch \"${CMAKE_CURRENT_BINARY_DIR}/resource-config.log\"")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1"
     "$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1"
     "$OutputEncoding = [System.Text.UTF8Encoding]::new()${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1"
     "$ErrorActionPreference = \"Stop\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1"
     "Set-PSDebug -Trace 1${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1"
     "& ${PROJECT_RESOURCE_EXCEL_COMMAND_ARGS}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
file(
  APPEND "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1"
  "& \"${CMAKE_COMMAND}\" -E touch \"${CMAKE_CURRENT_BINARY_DIR}/resource-config.log\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
)
unset(PROJECT_RESOURCE_EXCEL_COMMAND_ARGS)

if(NOT UNIX AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/resource-config.log"
    COMMAND "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" "-NoProfile" "-InputFormat" "None" "-ExecutionPolicy" "Bypass"
            "-NonInteractive" "-NoLogo" "-File" "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.ps1"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${PROJECT_INSTALL_RES_PBD_DIR}/config.pb" "${CMAKE_CURRENT_BINARY_DIR}/_generated/xml/xresconv.xml"
            ${PROJECT_RESOURCE_EXCEL_FILES}
    COMMENT "Generate excel resources [@${CMAKE_CURRENT_BINARY_DIR}]")
else()
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/resource-config.log"
    COMMAND "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-bytes.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${PROJECT_INSTALL_RES_PBD_DIR}/config.pb" "${CMAKE_CURRENT_BINARY_DIR}/_generated/xml/xresconv.xml"
            ${PROJECT_RESOURCE_EXCEL_FILES}
    COMMENT "Generate excel resources [@${CMAKE_CURRENT_BINARY_DIR}]")
endif()

add_custom_target(resource-config ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/resource-config.log")

set_property(TARGET "resource-config" PROPERTY FOLDER "${PROJECT_NAME}/config")

# ============= network protocols ==============
file(GLOB PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_PBDESC
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/pbdesc/*.proto"
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/pbdesc/*.proto")
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_PBDESC_DIR}/protocol/pbdesc" PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_PBDESC
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_PBDESC})

project_server_frame_create_protocol_target(
  protocol-net
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_PBDESC_DIR}"
  PROJECT_SERVER_FRAME_PROTO_LIBRARY_NET
  EXTERNAL_PROTO_PATH
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}"
  "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension"
  DEPENDS
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_EXTENSION}
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_PBDESC}
  PROTOCOLS
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_PBDESC}
  PUBLIC_LINK_LIBRARIES
  ${PROJECT_SERVER_FRAME_PROTO_LIBRARY_COMMON})

add_custom_target(protocol)
foreach(PROJECT_SERVER_FRAME_PROTO_SUB_TARGET protocol-extension protocol-common protocol-config protocol-net)
  add_dependencies(protocol ${PROJECT_SERVER_FRAME_PROTO_SUB_TARGET})
endforeach()

set_property(TARGET protocol PROPERTY FOLDER "${PROJECT_NAME}/protocol")

add_library(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol INTERFACE)
target_link_libraries(
  ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
  INTERFACE ${PROJECT_SERVER_FRAME_PROTO_LIBRARY_EXTENSION} ${PROJECT_SERVER_FRAME_PROTO_LIBRARY_COMMON}
            ${PROJECT_SERVER_FRAME_PROTO_LIBRARY_CONFIG} ${PROJECT_SERVER_FRAME_PROTO_LIBRARY_NET}
            ${PROJECT_SERVER_FRAME_PROTO_GENERATED_LOG})

set_property(TARGET protocol PROPERTY FOLDER "${PROJECT_NAME}/protocol")
set_property(TARGET ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol PROPERTY FOLDER "${PROJECT_NAME}/protocol")

add_custom_command(
  TARGET resource-config
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-E" "copy_if_different" "${PROJECT_GENERATED_PBD_DIR}/config.pb"
          "${PROJECT_INSTALL_RES_PBD_DIR}")

project_install_and_export_targets(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol)
