function(project_build_tools_optimize_sources)
  if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU|Clang|AppleClang" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach(PROTO_SRC ${ARGN})
      unset(PROTO_SRC_OPTIONS)
      get_source_file_property(PROTO_SRC_OPTIONS ${PROTO_SRC} COMPILE_OPTIONS)
      if(PROTO_SRC_OPTIONS)
        list(APPEND PROTO_SRC_OPTIONS "$<$<CONFIG:Debug>:-O2>")
      else()
        set(PROTO_SRC_OPTIONS "$<$<CONFIG:Debug>:-O2>")
      endif()

      set_source_files_properties(${PROTO_SRC} PROPERTIES COMPILE_OPTIONS "${PROTO_SRC_OPTIONS}")
    endforeach()
    unset(PROTO_SRC)
    unset(PROTO_SRC_OPTIONS)
  endif()
endfunction()

function(project_server_frame_create_protocol_target TARGET_NAME SANDBOX_PATH OUTPUT_HEAD_VAR OUTPUT_SOURCE_VAR)
  set(optionArgs "")
  set(oneValueArgs "")
  set(multiValueArgs EXTERNAL_PROTO_PATH DEPENDS PROTOCOLS)
  cmake_parse_arguments(project_server_frame_create_protocol_target "${optionArgs}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN})
  list(SORT project_server_frame_create_protocol_target_PROTOCOLS)
  unset(HEADERS)
  unset(SOURCES)

  # proto -> headers/sources
  unset(LAST_CREATED_DIRECTORY)
  unset(LAST_DIRECTORY_HEADERS)
  unset(LAST_DIRECTORY_SOURCES)
  unset(ADDITIONAL_CMAKE_COMMANDS)
  unset(TEMPORARY_CODE_FILES)
  set(FIND_PROTO_PATH_HINT "${SANDBOX_PATH}" ${project_server_frame_create_protocol_target_EXTERNAL_PROTO_PATH})
  foreach(FILE_PATH IN LISTS project_server_frame_create_protocol_target_PROTOCOLS)
    unset(FILE_RELATIVE_PATH)
    string(REPLACE "\\" "/" FILE_PATH_S "${FILE_PATH}")
    foreach(TRY_PROTO_PATH ${FIND_PROTO_PATH_HINT})
      string(REPLACE "\\" "/" TRY_PROTO_PATH_S "${TRY_PROTO_PATH}")
      string(LENGTH "${TRY_PROTO_PATH_S}/" TRY_PROTO_PATH_LENGTH)
      string(SUBSTRING "${FILE_PATH_S}" 0 ${TRY_PROTO_PATH_LENGTH} FILE_PATH_PREFIX)
      if(FILE_PATH_PREFIX STREQUAL "${TRY_PROTO_PATH_S}/")
        file(RELATIVE_PATH FILE_RELATIVE_PATH "${TRY_PROTO_PATH_S}" "${FILE_PATH_S}")
        break()
      endif()
    endforeach()
    if(NOT FILE_RELATIVE_PATH)
      message(FATAL_ERROR "${FILE_PATH} in not in one of ${FIND_PROTO_PATH_HINT}")
    endif()
    string(REGEX REPLACE "\\.proto$" "" FILE_RELATIVE_BASE "${FILE_RELATIVE_PATH}")
    get_filename_component(FILE_RELATIVE_DIR "${FILE_RELATIVE_PATH}" DIRECTORY)
    if(NOT "${FILE_RELATIVE_DIR}" STREQUAL "${LAST_CREATED_DIRECTORY}")
      if(LAST_DIRECTORY_HEADERS)
        list(
          APPEND
          ADDITIONAL_CMAKE_COMMANDS
          COMMAND
          "${CMAKE_COMMAND}"
          -E
          copy_if_different
          ${LAST_DIRECTORY_HEADERS}
          "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include/${LAST_CREATED_DIRECTORY}")
      endif()
      if(LAST_DIRECTORY_SOURCES)
        list(
          APPEND
          ADDITIONAL_CMAKE_COMMANDS
          COMMAND
          "${CMAKE_COMMAND}"
          -E
          copy_if_different
          ${LAST_DIRECTORY_SOURCES}
          "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/src/${LAST_CREATED_DIRECTORY}")
      endif()

      set(LAST_CREATED_DIRECTORY "${FILE_RELATIVE_DIR}")
      if(NOT EXISTS "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include/${LAST_CREATED_DIRECTORY}")
        file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include/${LAST_CREATED_DIRECTORY}")
      endif()
      if(NOT EXISTS "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/src/${LAST_CREATED_DIRECTORY}")
        file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/src/${LAST_CREATED_DIRECTORY}")
      endif()
    endif()
    list(APPEND LAST_DIRECTORY_HEADERS "${SANDBOX_PATH}/${FILE_RELATIVE_BASE}.pb.h")
    list(APPEND LAST_DIRECTORY_SOURCES "${SANDBOX_PATH}/${FILE_RELATIVE_BASE}.pb.cc")
    list(APPEND TEMPORARY_CODE_FILES ${LAST_DIRECTORY_HEADERS} ${LAST_DIRECTORY_SOURCES})
    list(APPEND HEADERS "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include/${FILE_RELATIVE_BASE}.pb.h")
    list(APPEND SOURCES "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/src/${FILE_RELATIVE_BASE}.pb.cc")
  endforeach()
  if(LAST_DIRECTORY_HEADERS)
    list(
      APPEND
      ADDITIONAL_CMAKE_COMMANDS
      COMMAND
      "${CMAKE_COMMAND}"
      -E
      copy_if_different
      ${LAST_DIRECTORY_HEADERS}
      "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include/${LAST_CREATED_DIRECTORY}")
  endif()
  if(LAST_DIRECTORY_SOURCES)
    list(
      APPEND
      ADDITIONAL_CMAKE_COMMANDS
      COMMAND
      "${CMAKE_COMMAND}"
      -E
      copy_if_different
      ${LAST_DIRECTORY_SOURCES}
      "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/src/${LAST_CREATED_DIRECTORY}")
  endif()
  list(APPEND TEMPORARY_CODE_FILES ${LAST_DIRECTORY_HEADERS} ${LAST_DIRECTORY_SOURCES})

  set(PROTOC_PROTO_PATH_ARGS --proto_path "${SANDBOX_PATH}")
  foreach(ADDTIONAL_PROTO_PATH ${project_server_frame_create_protocol_target_EXTERNAL_PROTO_PATH})
    list(APPEND PROTOC_PROTO_PATH_ARGS --proto_path "${ADDTIONAL_PROTO_PATH}")
  endforeach()
  list(
    APPEND
    PROTOC_PROTO_PATH_ARGS
    --proto_path
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}"
    --proto_path
    "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
    --proto_path
    "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include")

  add_custom_command(
    OUTPUT ${HEADERS} ${SOURCES}
    COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" ${PROTOC_PROTO_PATH_ARGS} --cpp_out
      "${SANDBOX_PATH}"
      # Protocol buffer files
      ${project_server_frame_create_protocol_target_PROTOCOLS} ${ADDITIONAL_CMAKE_COMMANDS}
    COMMAND "${CMAKE_COMMAND}" -E remove -f ${TEMPORARY_CODE_FILES}
    WORKING_DIRECTORY "${SANDBOX_PATH}"
    DEPENDS ${project_server_frame_create_protocol_target_PROTOCOLS}
            ${project_server_frame_create_protocol_target_DEPENDS}
    COMMENT "Generate [@${SANDBOX_PATH}] ${project_server_frame_create_protocol_target_PROTOCOLS}")
  add_custom_target(
    ${TARGET_NAME}
    DEPENDS ${HEADERS} ${SOURCES}
    SOURCES ${HEADERS} ${SOURCES})
  project_build_tools_patch_protobuf_sources(${HEADERS} ${SOURCES})
  project_build_tools_optimize_sources(${HEADERS} ${SOURCES})

  if(MSVC)
    set_property(TARGET "${TARGET_NAME}" PROPERTY FOLDER "${PROJECT_NAME}")
  endif()

  set(${OUTPUT_HEAD_VAR}
      ${${OUTPUT_HEAD_VAR}} ${HEADERS}
      PARENT_SCOPE)
  set(${OUTPUT_SOURCE_VAR}
      ${${OUTPUT_SOURCE_VAR}} ${SOURCES}
      PARENT_SCOPE)
endfunction()

function(project_server_frame_create_protocol_sandbox OUTPUT_DIR OUTPUT_VAR)
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")
  unset(OUTPUT_FILES)
  foreach(PROTO_FILE ${ARGN})
    get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME)
    file(CREATE_LINK "${PROTO_FILE}" "${OUTPUT_DIR}/${PROTO_NAME}" COPY_ON_ERROR SYMBOLIC)
    list(APPEND OUTPUT_FILES "${OUTPUT_DIR}/${PROTO_NAME}")
  endforeach()

  set(${OUTPUT_VAR}
      ${${OUTPUT_VAR}} ${OUTPUT_FILES}
      PARENT_SCOPE)
endfunction()

unset(PROJECT_SERVER_FRAME_PROTO_GENERATED_HEADERS)
unset(PROJECT_SERVER_FRAME_PROTO_GENERATED_SOURCES)
# ============= common protocols =============
file(GLOB PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/common/*.proto"
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/common/*.proto")
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}/protocol/common" PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON})
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}/protocol/common/extensions/v3"
  PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto"
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader_ue.proto")
list(REMOVE_DUPLICATES PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON)

project_server_frame_create_protocol_target(
  protocol-common
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}"
  PROJECT_SERVER_FRAME_PROTO_GENERATED_HEADERS
  PROJECT_SERVER_FRAME_PROTO_GENERATED_SOURCES
  DEPENDS
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
  PROTOCOLS
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON})

# ============= configure protocols =============
file(GLOB PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/config/*.proto"
     "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/config/*.proto")
project_server_frame_create_protocol_sandbox(
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}/protocol/config" PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG} "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/pb_header_v3.proto"
  "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto")
list(REMOVE_DUPLICATES PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG)

project_server_frame_create_protocol_target(
  protocol-config
  "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
  PROJECT_SERVER_FRAME_PROTO_GENERATED_HEADERS
  PROJECT_SERVER_FRAME_PROTO_GENERATED_SOURCES
  EXTERNAL_PROTO_PATH
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}"
  "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension"
  DEPENDS
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG}
  PROTOCOLS
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG})

add_custom_command(
  OUTPUT "${PROJECT_INSTALL_RES_PBD_DIR}/config.pb"
  COMMAND "${CMAKE_COMMAND}" -E remove -f "${PROJECT_INSTALL_RES_PBD_DIR}/config.pb"
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path
    "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}" --proto_path "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
    --proto_path "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}" --proto_path "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
    --proto_path "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include" -o "${PROJECT_INSTALL_RES_PBD_DIR}/config.pb"
    # Protocol buffer files
    ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG} ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON}
    "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe/atapp_conf.proto"
    "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include/libatbus_protocol.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/any.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/duration.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/timestamp.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/descriptor.proto"
  WORKING_DIRECTORY "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
  DEPENDS ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON} ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_CONFIG}
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
if(MSVC)
  set_property(TARGET "resource-config" PROPERTY FOLDER "${PROJECT_NAME}")
endif()

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
  PROJECT_SERVER_FRAME_PROTO_GENERATED_HEADERS
  PROJECT_SERVER_FRAME_PROTO_GENERATED_SOURCES
  EXTERNAL_PROTO_PATH
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}
  "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}"
  "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension"
  DEPENDS
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_COMMON}
  ${PROJECT_SERVER_FRAME_PROTO_ORIGIN_LIST_PBDESC}
  PROTOCOLS
  ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_PBDESC})

add_custom_target(protocol)
foreach(PROJECT_SERVER_FRAME_PROTO_SUB_TARGET protocol-common protocol-config protocol-net)
  add_dependencies(protocol ${PROJECT_SERVER_FRAME_PROTO_SUB_TARGET})
endforeach()
if(MSVC)
  set_property(TARGET protocol PROPERTY FOLDER "${PROJECT_NAME}")
endif()

source_group_by_dir(PROJECT_SERVER_FRAME_PROTO_GENERATED_HEADERS PROJECT_SERVER_FRAME_PROTO_GENERATED_SOURCES)
if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows|MinGW|WindowsStore" AND (BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY
                                                                  ))
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol SHARED ${PROJECT_SERVER_FRAME_PROTO_GENERATED_HEADERS}
                                                               ${PROJECT_SERVER_FRAME_PROTO_GENERATED_SOURCES})

  project_tool_split_target_debug_sybmol(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol)
else()
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol STATIC ${PROJECT_SERVER_FRAME_PROTO_GENERATED_HEADERS}
                                                               ${PROJECT_SERVER_FRAME_PROTO_GENERATED_SOURCES})
endif()
set_target_properties(
  ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
  PROPERTIES C_VISIBILITY_PRESET "default"
             CXX_VISIBILITY_PRESET "default"
             VERSION "${PROJECT_VERSION}"
             SOVERSION "${PROJECT_VERSION}"
             WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
target_include_directories(
  ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
  PUBLIC "$<BUILD_INTERFACE:${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include>"
         "$<BUILD_INTERFACE:${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include>"
         "$<BUILD_INTERFACE:${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include>"
         "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_options(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
                       PRIVATE ${PROJECT_COMMON_PROTOCOL_SOURCE_COMPILE_OPTIONS})
if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
  target_link_options(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
endif()

add_dependencies(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol protocol)
target_link_libraries(${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
                      PUBLIC ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME} atbus-protocol atapp-protocol)

if(MSVC)
  set_property(TARGET "${PROJECT_SERVER_FRAME_LIB_LINK}-protocol" PROPERTY FOLDER "${PROJECT_NAME}")
endif()

install(
  TARGETS ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
  EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}")

install(
  DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include/protocol"
  DESTINATION "include"
  USE_SOURCE_PERMISSIONS FILES_MATCHING
  REGEX ".+\\.pb\\.h?$"
  PATTERN ".svn" EXCLUDE
  PATTERN ".git" EXCLUDE)
