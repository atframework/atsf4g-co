include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

echowithcolor(COLOR GREEN "-- Configure ${ATSF4G_APP_NAME} on ${CMAKE_CURRENT_LIST_DIR}")
if(NOT ATFRAMEWORK_EXPORT_TARGET_NAME)
  set(ATFRAMEWORK_EXPORT_TARGET_NAME "atframework-target")
endif()

# ============ atgateway ============
if(NOT PROJECT_GENERATED_DIR)
  set(PROJECT_GENERATED_DIR "${CMAKE_BINARY_DIR}/_generated")
endif()
set(ATGATEWAY_GENERATED_SERVER_SDK_DIR "${PROJECT_GENERATED_DIR}/service/atgateway/server_sdk")
set(ATGATEWAY_GENERATED_PROTOCOL_SDK_DIR "${PROJECT_GENERATED_DIR}/service/atgateway/protocol_sdk")
set(ATGATEWAY_GENERATED_SERVICE_DIR "${PROJECT_GENERATED_DIR}/service/atgateway/service")
file(MAKE_DIRECTORY "${ATGATEWAY_GENERATED_SERVER_SDK_DIR}/atgateway/protocols")
file(MAKE_DIRECTORY "${ATGATEWAY_GENERATED_PROTOCOL_SDK_DIR}/atgateway/protocols/v1")
file(MAKE_DIRECTORY "${ATGATEWAY_GENERATED_SERVICE_DIR}/atgateway/protocols")
set(ATSF4G_APP_SDK_LIBATGW_SERVER_SDK_TARGET_NAME ${ATFRAMEWORK_SERVICE_GATEWAY_SERVER_SDK})
set(ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SDK_TARGET_NAME ${ATFRAMEWORK_SERVICE_GATEWAY_CLIENT_SDK})
set(ATSF4G_APP_SDK_LIBATGW_SERVER_CONFIG_TARGET_NAME atgateway-server-config)

set(ATSF4G_APP_SDK_LIBATGW_PROTOCOL_API_HEADER_LIST
    "atgateway/protocols/v1/libatgw_protocol_sdk.h" "atgateway/protocols/v1/libatgw_protocol_config.h"
    "atgateway/protocols/libatgw_protocol_api.h")

set(ATSF4G_APP_SDK_LIBATGW_PROTOCOL_API_GENERATED_HEADER_LIST
    "${ATGATEWAY_GENERATED_PROTOCOL_SDK_DIR}/atgateway/protocols/v1/libatgw_protocol_sdk_generated.h")

set(ATSF4G_APP_SDK_LIBATGW_PROTOCOL_API_SOURCE_LIST "atgateway/protocols/v1/libatgw_protocol_sdk.cpp"
                                                    "atgateway/protocols/libatgw_protocol_api.cpp")

set(ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_HEADER_LIST "atgateway/protocols/libatgw_server_protocol.h")

set(ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_GENERATED_HEADER_LIST
    "${ATGATEWAY_GENERATED_SERVER_SDK_DIR}/atgateway/protocols/libatgw_server_protocol.pb.h")

set(ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_GENERATED_SOURCE_LIST
    "${ATGATEWAY_GENERATED_SERVER_SDK_DIR}/atgateway/protocols/libatgw_server_protocol.pb.cc")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/protocols")
add_custom_command(
  OUTPUT ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_GENERATED_HEADER_LIST}
         ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_GENERATED_SOURCE_LIST}
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path "${CMAKE_CURRENT_LIST_DIR}"
    "--cpp_out=dllexport_decl=LIBATGW_SERVER_API:${CMAKE_CURRENT_BINARY_DIR}/protocols"
    "${CMAKE_CURRENT_LIST_DIR}/atgateway/protocols/libatgw_server_protocol.proto"
  COMMAND
    "${CMAKE_COMMAND}" "-E" "copy_if_different"
    "${CMAKE_CURRENT_BINARY_DIR}/protocols/atgateway/protocols/libatgw_server_protocol.pb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/protocols/atgateway/protocols/libatgw_server_protocol.pb.cc"
    "${ATGATEWAY_GENERATED_SERVER_SDK_DIR}/atgateway/protocols/"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/atgateway/protocols/libatgw_server_protocol.proto"
          "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}"
  COMMENT "Generate libatgw_server_protocol.pb.h, libatgw_server_protocol.pb.cc")

project_build_tools_patch_protobuf_sources(
  "${ATGATEWAY_GENERATED_SERVER_SDK_DIR}/atgateway/protocols/libatgw_server_protocol.pb.cc")

add_custom_command(
  OUTPUT "${ATGATEWAY_GENERATED_SERVICE_DIR}/atgateway/protocols/libatgw_server_config.pb.h"
         "${ATGATEWAY_GENERATED_SERVICE_DIR}/atgateway/protocols/libatgw_server_config.pb.cc"
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path "${CMAKE_CURRENT_LIST_DIR}" --proto_path
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/include" --proto_path "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include" --proto_path
    "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include"
    "--cpp_out=dllexport_decl=LIBATGW_SERVER_CONFIG_API:${CMAKE_CURRENT_BINARY_DIR}/protocols"
    "${CMAKE_CURRENT_LIST_DIR}/atgateway/protocols/libatgw_server_config.proto"
  COMMAND
    "${CMAKE_COMMAND}" "-E" "copy_if_different"
    "${CMAKE_CURRENT_BINARY_DIR}/protocols/atgateway/protocols/libatgw_server_config.pb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/protocols/atgateway/protocols/libatgw_server_config.pb.cc"
    "${ATGATEWAY_GENERATED_SERVICE_DIR}/atgateway/protocols/"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/atgateway/protocols/libatgw_server_config.proto"
          "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}"
  COMMENT "Generate libatgw_server_config.pb.h, libatgw_server_config.pb.cc")

project_build_tools_patch_protobuf_sources(
  "${ATGATEWAY_GENERATED_SERVICE_DIR}/atgateway/protocols/libatgw_server_config.pb.cc")

# ============ libatbus - src ============
if(ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_FLATC_EXECUTABLE)
  set(ATFRAMEWORK_LIBATGW_PROTOCOL_SDK_FLAC_OPTIONS "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_FLATC_EXECUTABLE}" --cpp
                                                    --allow-non-utf8 --gen-mutable)
  if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    list(APPEND ATFRAMEWORK_LIBATGW_PROTOCOL_SDK_FLAC_OPTIONS --gen-nullable)
  endif()
  if(COMPILER_OPTION_RECOMMEND_CXX_STANDARD GREATER_EQUAL 17)
    list(APPEND ATFRAMEWORK_LIBATGW_PROTOCOL_SDK_FLAC_OPTIONS --cpp-std c++17 --cpp-static-reflection)
  endif()
  list(
    APPEND
    ATFRAMEWORK_LIBATGW_PROTOCOL_SDK_FLAC_OPTIONS
    -o
    "${CMAKE_CURRENT_BINARY_DIR}"
    -I
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/atgateway/protocols/v1/libatgw_protocol_sdk.fbs")
  add_custom_command(
    OUTPUT ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_API_GENERATED_HEADER_LIST}
    COMMAND ${ATFRAMEWORK_LIBATGW_PROTOCOL_SDK_FLAC_OPTIONS}
    COMMAND "${CMAKE_COMMAND}" "-E" "copy_if_different" "${CMAKE_CURRENT_BINARY_DIR}/libatgw_protocol_sdk_generated.h"
            "${ATGATEWAY_GENERATED_PROTOCOL_SDK_DIR}/atgateway/protocols/v1/"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/atgateway/protocols/v1/libatgw_protocol_sdk.fbs"
    COMMENT "Generate libatgw_protocol_sdk_generated.h")
endif()

# ATSF4G_APP_SDK_LIBATGW_SERVER_CONFIG_TARGET_NAME
atframework_add_library(
  ${ATSF4G_APP_SDK_LIBATGW_SERVER_CONFIG_TARGET_NAME}
  PUBLIC_SYMBOL_DECL
  LIBATGW_SERVER_CONFIG_API
  FOLDER_PATH
  "atframework/atgateway/sdk"
  PUBLIC_INCLUDE_DIRECTORY
  "${ATGATEWAY_GENERATED_SERVICE_DIR}"
  PUBLIC_LINK_NAMES
  ${ATFRAMEWORK_LIBATAPP_PROTOCOL_LINK_NAME}
  GENERATED_DIR
  "${ATGATEWAY_GENERATED_SERVICE_DIR}"
  GENERATED_HEADERS
  "${ATGATEWAY_GENERATED_SERVICE_DIR}/atgateway/protocols/libatgw_server_config.pb.h"
  GENERATED_SOURCES
  "${ATGATEWAY_GENERATED_SERVICE_DIR}/atgateway/protocols/libatgw_server_config.pb.cc")

# ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SDK_TARGET_NAME
atframework_add_library(
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SDK_TARGET_NAME}
  PUBLIC_SYMBOL_DECL
  LIBATGW_PROTOCOL_API
  FOLDER_PATH
  "atframework/atgateway/sdk"
  PUBLIC_INCLUDE_DIRECTORY
  "${ATGATEWAY_GENERATED_PROTOCOL_SDK_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}"
  PUBLIC_LINK_NAMES
  ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}
  ${ATFRAMEWORK_LIBATBUS_LINK_NAME}
  ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_FLATBUFFERS_LINK_NAME}
  ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME}
  GENERATED_DIR
  "${ATGATEWAY_GENERATED_PROTOCOL_SDK_DIR}"
  HEADERS
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_API_HEADER_LIST}
  SOURCES
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_API_SOURCE_LIST}
  GENERATED_HEADERS
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_API_GENERATED_HEADER_LIST})

if(COMPILER_OPTION_RECOMMEND_CXX_STANDARD GREATER_EQUAL 17)
  target_compile_definitions(${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SDK_TARGET_NAME}
                             PUBLIC ATFRAMEWORK_GATEWAY_MACRO_FLATC_USE_SCOPE_ENUM=1)
endif()

atframework_install_files(
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SDK_TARGET_NAME}
  INSTALL_FILES
  "${CMAKE_CURRENT_LIST_DIR}/protocols/v1/libatgw_protocol_config.h"
  "${CMAKE_CURRENT_LIST_DIR}/protocols/v1/libatgw_protocol_sdk.h"
  "${CMAKE_CURRENT_LIST_DIR}/protocols/v1/libatgw_protocol_sdk.fbs"
  INSTALL_DESTINATION
  "${CMAKE_INSTALL_DATADIR}/atgateway/protocols/v1/")

atframework_install_files(
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SDK_TARGET_NAME} INSTALL_FILES
  "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_protocol_api.h" INSTALL_DESTINATION
  "${CMAKE_INSTALL_DATADIR}/atgateway/protocols/")

# ATSF4G_APP_SDK_LIBATGW_SERVER_SDK_TARGET_NAME
atframework_add_library(
  ${ATSF4G_APP_SDK_LIBATGW_SERVER_SDK_TARGET_NAME}
  PUBLIC_SYMBOL_DECL
  LIBATGW_SERVER_API
  FOLDER_PATH
  "atframework/atgateway/sdk"
  PUBLIC_INCLUDE_DIRECTORY
  "${ATGATEWAY_GENERATED_SERVER_SDK_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}"
  PUBLIC_LINK_NAMES
  ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME}
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SDK_TARGET_NAME}
  GENERATED_DIR
  "${ATGATEWAY_GENERATED_SERVER_SDK_DIR}"
  HEADERS
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_HEADER_LIST}
  GENERATED_HEADERS
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_GENERATED_HEADER_LIST}
  GENERATED_SOURCES
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_GENERATED_SOURCE_LIST})

atframework_install_files(
  ${ATSF4G_APP_SDK_LIBATGW_SERVER_SDK_TARGET_NAME}
  INSTALL_FILES
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_HEADER_LIST}
  ${ATSF4G_APP_SDK_LIBATGW_PROTOCOL_SERVICE_SDK_GENERATED_HEADER_LIST}
  INSTALL_DESTINATION
  "${CMAKE_INSTALL_INCLUDEDIR}/atgateway/protocols/")
