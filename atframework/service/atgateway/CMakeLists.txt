set(ATSF4G_APP_NAME atgateway)
echowithcolor(COLOR GREEN "-- Configure ${ATSF4G_APP_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

# ============ atgateway ============

set(ATSF4G_APP_SRC_LIST
    "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.pb.h"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.pb.cc"
    "${CMAKE_CURRENT_LIST_DIR}/session.h"
    "${CMAKE_CURRENT_LIST_DIR}/session_manager.h"
    "${CMAKE_CURRENT_LIST_DIR}/atgateway.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/session_manager.cpp")
source_group_by_dir(ATSF4G_APP_SRC_LIST)

set(ATSF4G_APP_PROTO_SRC_LIST
    "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner.h"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner_config.h"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner_generated.h"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_protocol.h"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/proto_base.h"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_protocol.pb.h"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_protocol.pb.cc"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/protocols/proto_base.cpp")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/atframe/${ATSF4G_APP_NAME}/bin")

if(NOT EXISTS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_protocol.pb.h"
         "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_protocol.pb.cc"
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path "${CMAKE_CURRENT_LIST_DIR}/protocols"
    "--cpp_out=${CMAKE_CURRENT_LIST_DIR}/protocols" "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_protocol.proto"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_protocol.proto"
  COMMENT "Generate libatgw_server_protocol.pb.h, libatgw_server_protocol.pb.cc")

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.pb.h"
         "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.pb.cc"
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path "${CMAKE_CURRENT_LIST_DIR}/protocols"
    --proto_path "${CMAKE_CURRENT_LIST_DIR}/protocols" --proto_path "${PROJECT_THIRD_PARTY_INSTALL_DIR}/include"
    --proto_path "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include" --proto_path "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include"
    "--cpp_out=${CMAKE_CURRENT_LIST_DIR}/protocols" "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.proto"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.proto"
  COMMENT "Generate libatgw_server_config.pb.h, libatgw_server_config.pb.cc")

project_build_tools_patch_protobuf_sources("${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.pb.h"
                                           "${CMAKE_CURRENT_LIST_DIR}/protocols/libatgw_server_config.pb.cc")
# ============ libatbus - src ============
if(TARGET flatbuffers::flatc)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner_generated.h"
    COMMAND
      flatbuffers::flatc --cpp --allow-non-utf8 --gen-mutable -o "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1" -I
      "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1"
      "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner.fbs"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner.fbs"
    COMMENT "Generate libatgw_proto_inner_generated.h")
  set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/protocols/inner_v1/libatgw_proto_inner_generated.h"
                              PROPERTIES GENERATED TRUE)
endif()

add_library(${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME} STATIC ${ATSF4G_APP_PROTO_SRC_LIST})
set_target_properties(
  ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME}
  PROPERTIES C_VISIBILITY_PRESET "default"
             CXX_VISIBILITY_PRESET "default"
             WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
target_link_libraries(
  ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME}
  PUBLIC ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME} ${ATFRAMEWORK_LIBATBUS_LINK_NAME}
         ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME})
target_include_directories(
  ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME}
  PUBLIC "$<BUILD_INTERFACE:${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_FLATBUFFER_INC_DIR}>"
         "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/protocols>")
target_compile_options(${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME} PRIVATE ${PROJECT_LIBATBUS_PRIVATE_COMPILE_OPTIONS})
if(MSVC)
  set(PROJECT_LIBATBUS_PROTOCOL_COMPILE_OPTIONS "/wd4244")
  if(MSVC_VERSION GREATER_EQUAL 1922)
    # see https://docs.microsoft.com/en-us/cpp/overview/cpp-conformance-improvements?view=vs-2019#improvements_162 for
    # detail
    list(APPEND PROJECT_LIBATBUS_PROTOCOL_COMPILE_OPTIONS "/wd5054")
  endif()
  if(MSVC_VERSION GREATER_EQUAL 1925)
    list(APPEND PROJECT_LIBATBUS_PROTOCOL_COMPILE_OPTIONS "/wd4996")
  endif()
  target_compile_options(${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME} PRIVATE ${PROJECT_LIBATBUS_PROTOCOL_COMPILE_OPTIONS})
endif()

add_executable(${ATSF4G_APP_NAME} ${ATSF4G_APP_SRC_LIST})

if(MSVC)
  set_property(TARGET ${ATSF4G_APP_NAME} PROPERTY FOLDER "atframework/service")
  add_target_properties(${ATSF4G_APP_NAME} LINK_FLAGS /NODEFAULTLIB:library)
endif(MSVC)

target_link_libraries(${ATSF4G_APP_NAME} ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME}
                      ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME})

target_compile_options(${ATSF4G_APP_NAME} PRIVATE ${ATFRAMEWORK_SREVICE_PRIVATE_OPTIONS})

target_include_directories(${ATSF4G_APP_NAME} PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>")

install(
  TARGETS ${ATSF4G_APP_NAME} ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME}
  EXPORT "${ATSF4G_APP_NAME}-target"
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

export(
  TARGETS ${ATSF4G_APP_NAME} ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME}
  NAMESPACE "atframework::"
  FILE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATSF4G_APP_NAME}/${ATSF4G_APP_NAME}-target.cmake")

install(
  EXPORT "${ATSF4G_APP_NAME}-target"
  NAMESPACE "atframework::"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${ATSF4G_APP_NAME}")
