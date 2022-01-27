unset(PROJECT_SERVER_FRAME_PUBLIC_LINK_NAMES)
unset(PROJECT_SERVER_FRAME_PUBLIC_INCLUDE_DIRS)
unset(PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS)

list(APPEND PROJECT_SERVER_FRAME_PUBLIC_INCLUDE_DIRS ${PROJECT_SERVER_FRAME_INC_DIR})

# generated config loader protocol
add_custom_command(
  OUTPUT "${PROJECT_SERVER_FRAME_SRC_DIR}/config/excel/pb_header_v3.pb.h"
         "${PROJECT_SERVER_FRAME_SRC_DIR}/config/excel/pb_header_v3.pb.cc"
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path "./" --cpp_out "./"
    # -o "${PROJECT_INSTALL_RES_PBD_DIR}/config.pb"
    "./pb_header_v3.proto"
  WORKING_DIRECTORY "${PROJECT_SERVER_FRAME_SRC_DIR}/config/excel"
  DEPENDS "${PROJECT_SERVER_FRAME_SRC_DIR}/config/excel/pb_header_v3.proto"
  COMMENT "Generate [@${PROJECT_SERVER_FRAME_SRC_DIR}/config/excel] ./pb_header_v3.proto")
project_build_tools_patch_protobuf_sources("${PROJECT_SERVER_FRAME_SRC_DIR}/config/excel/pb_header_v3.pb.h"
                                           "${PROJECT_SERVER_FRAME_SRC_DIR}/config/excel/pb_header_v3.pb.cc")

configure_file("${CMAKE_CURRENT_LIST_DIR}/config/server_frame_build_feature.h.in"
               "${CMAKE_CURRENT_LIST_DIR}/config/server_frame_build_feature.h" ESCAPE_QUOTES @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/config/server_frame_build_feature.cpp.in"
               "${CMAKE_CURRENT_LIST_DIR}/config/server_frame_build_feature.cpp" ESCAPE_QUOTES @ONLY)
list(APPEND SRC_LIST "${CMAKE_CURRENT_LIST_DIR}/config/server_frame_build_feature.h"
     "${CMAKE_CURRENT_LIST_DIR}/config/server_frame_build_feature.cpp")

# build libserver_frame.so
file(
  GLOB_RECURSE
  SRC_LIST_ORIGIN
  ${PROJECT_SERVER_FRAME_SRC_DIR}/*.h
  ${PROJECT_SERVER_FRAME_SRC_DIR}/*.hpp
  ${PROJECT_SERVER_FRAME_SRC_DIR}/*.c
  ${PROJECT_SERVER_FRAME_SRC_DIR}/*.cpp
  ${PROJECT_SERVER_FRAME_SRC_DIR}/*.cc)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/skip-glob-sources.txt" "")
foreach(SRC_FILE_REALPATH IN LISTS SRC_LIST_ORIGIN)
  get_filename_component(SRC_DIRPATH_REALPATH ${SRC_FILE_REALPATH} DIRECTORY)
  if(SRC_FILE_REALPATH MATCHES "^${PROJECT_SERVER_FRAME_SRC_DIR}[/\\\\]config[/\\\\]server_frame_build_feature.(h|cpp)")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/skip-glob-sources.txt" "${SRC_FILE_REALPATH}\r\n")
    continue()
  endif()
  if(SRC_FILE_REALPATH MATCHES "^${PROJECT_SERVER_FRAME_SRC_DIR}[/\\\\]config[/\\\\]excel[/\\\\]")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/skip-glob-sources.txt" "${SRC_FILE_REALPATH}\r\n")
    continue()
  endif()
  if(SRC_FILE_REALPATH MATCHES "^${PROJECT_SERVER_FRAME_SRC_DIR}[/\\\\]protocol[/\\\\].*")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/skip-glob-sources.txt" "${SRC_FILE_REALPATH}\r\n")
    continue()
  endif()

  list(APPEND SRC_LIST ${SRC_FILE_REALPATH})
endforeach()
unset(SRC_FILE_REALPATH)
unset(SRC_DIRPATH_REALPATH)

# add generated files
include("${PROJECT_SERVER_FRAME_BAS_DIR}/generate_proto_source.cmake")
include("${PROJECT_SERVER_FRAME_BAS_DIR}/generate_config_codes.cmake")
list(REMOVE_DUPLICATES SRC_LIST)

source_group_by_dir(SRC_LIST)

list(APPEND PROJECT_SERVER_FRAME_PUBLIC_LINK_NAMES ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME}
     ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME})

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-unused-parameter PROJECT_SERVER_FRAME_LINT_NO_UNUSED_PARAMETER)
if(PROJECT_SERVER_FRAME_LINT_NO_UNUSED_PARAMETER)
  list(APPEND PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS -Wno-unused-parameter)
endif()

if(NOT WIN32 AND (BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY))
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK} SHARED ${SRC_LIST})
else()
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK} STATIC ${SRC_LIST})
endif()

target_link_libraries(${PROJECT_SERVER_FRAME_LIB_LINK} PUBLIC ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
                                                              ${PROJECT_SERVER_FRAME_PUBLIC_LINK_NAMES})

target_include_directories(
  ${PROJECT_SERVER_FRAME_LIB_LINK}
  PUBLIC "$<BUILD_INTERFACE:${PROJECT_SERVER_FRAME_PUBLIC_INCLUDE_DIRS}>"
         "$<BUILD_INTERFACE:${PROJECT_SERVER_FRAME_PUBLIC_INCLUDE_DIRS}/protocol/config>"
         "$<INSTALL_INTERFACE:include>" "$<INSTALL_INTERFACE:include/protocol/config>")

target_compile_options(${PROJECT_SERVER_FRAME_LIB_LINK} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
if(PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS)
  target_compile_options(${PROJECT_SERVER_FRAME_LIB_LINK} PUBLIC ${PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS})
endif()

project_install_and_export_targets(${PROJECT_SERVER_FRAME_LIB_LINK})
