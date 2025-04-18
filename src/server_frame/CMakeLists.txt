unset(PROJECT_SERVER_FRAME_PUBLIC_LINK_NAMES)
unset(PROJECT_SERVER_FRAME_PUBLIC_INCLUDE_DIRS)
unset(PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS)

set(PROJECT_SERVER_FRAME_PRECOMPILE_HEADERS
    [["dispatcher/task_type_traits.h"]] [["dispatcher/task_action_ss_req_base.h"]]
    [["dispatcher/task_action_no_req_base.h"]] [["rpc/rpc_shared_message.h"]] [["rpc/rpc_common_types.h"]]
    [["utility/protobuf_mini_dumper.h"]])

list(APPEND PROJECT_SERVER_FRAME_PUBLIC_INCLUDE_DIRS ${PROJECT_SERVER_FRAME_INC_DIR})

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
  if(SRC_FILE_REALPATH MATCHES "^${PROJECT_SERVER_FRAME_SRC_DIR}[/\\\\]config[/\\\\]")
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

source_group(TREE ${PROJECT_SERVER_FRAME_SRC_DIR} FILES ${SRC_LIST})

list(APPEND PROJECT_SERVER_FRAME_PUBLIC_LINK_NAMES ${ATFRAMEWORK_SERVICE_GATEWAY_SERVER_SDK}
     ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME})

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
if(TARGET Threads::Threads)
  list(APPEND PROJECT_SERVER_FRAME_PUBLIC_LINK_NAMES Threads::Threads)
elseif(CMAKE_USE_PTHREADS_INIT OR ATFRAMEWORK_CMAKE_TOOLSET_TEST_FLAG_PTHREAD)
  list(APPEND PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS "-pthread")
endif()

if(BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK} SHARED ${SRC_LIST})
  set(PROJECT_SERVER_FRAME_LIB_INSTALL_RPATH
      "${PROJECT_RPATH_ORIGIN}"
      "${PROJECT_RPATH_ORIGIN}/../../${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${PROJECT_SERVER_FRAME_LIB_LINK}/${CMAKE_INSTALL_LIBDIR}"
      ${PROJECT_INSTALL_RPATH}
      "${PROJECT_RPATH_ORIGIN}/../../../${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/__shared/${CMAKE_INSTALL_LIBDIR}"
      ${PROJECT_EXTERNAL_RPATH})

  set_target_properties(
    ${PROJECT_SERVER_FRAME_LIB_LINK}
    PROPERTIES VERSION "${PROJECT_VERSION}"
               SOVERSION "${PROJECT_VERSION}"
               INSTALL_RPATH "${PROJECT_SERVER_FRAME_LIB_INSTALL_RPATH}")

  project_tool_split_target_debug_sybmol(${PROJECT_SERVER_FRAME_LIB_LINK})
  project_build_tools_set_shared_library_declaration(SERVER_FRAME_API "${PROJECT_SERVER_FRAME_LIB_LINK}")
  target_compile_definitions(${PROJECT_SERVER_FRAME_LIB_LINK} PUBLIC "SERVER_FRAME_API_DLL=1")
else()
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK} STATIC ${SRC_LIST})
  set_target_properties(${PROJECT_SERVER_FRAME_LIB_LINK} PROPERTIES VERSION "${PROJECT_VERSION}")
  project_build_tools_set_static_library_declaration(SERVER_FRAME_API "${PROJECT_SERVER_FRAME_LIB_LINK}")
endif()
target_compile_definitions(${PROJECT_SERVER_FRAME_LIB_LINK} PRIVATE "SERVER_FRAME_API_NATIVE=1")

set_target_properties(${PROJECT_SERVER_FRAME_LIB_LINK} PROPERTIES C_VISIBILITY_PRESET "hidden" CXX_VISIBILITY_PRESET
                                                                                               "hidden")

add_dependencies(${PROJECT_SERVER_FRAME_LIB_LINK} protocol config-loader)
target_link_libraries(${PROJECT_SERVER_FRAME_LIB_LINK} PUBLIC ${PROJECT_SERVER_FRAME_LIB_LINK}-config
                                                              ${PROJECT_SERVER_FRAME_PUBLIC_LINK_NAMES})

target_include_directories(
  ${PROJECT_SERVER_FRAME_LIB_LINK} PUBLIC "$<BUILD_INTERFACE:${PROJECT_SERVER_FRAME_PUBLIC_INCLUDE_DIRS}>"
                                          "$<INSTALL_INTERFACE:include>")

target_compile_options(${PROJECT_SERVER_FRAME_LIB_LINK} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
  target_link_options(${PROJECT_SERVER_FRAME_LIB_LINK} PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
endif()

if(PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS)
  target_compile_options(${PROJECT_SERVER_FRAME_LIB_LINK} PUBLIC ${PROJECT_SERVER_FRAME_PUBLIC_COMPILE_OPTIONS})
endif()

project_build_tools_target_precompile_headers(
  ${PROJECT_SERVER_FRAME_LIB_LINK} PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${PROJECT_SERVER_FRAME_PRECOMPILE_HEADERS}>>")

set_property(TARGET ${PROJECT_SERVER_FRAME_LIB_LINK} PROPERTY FOLDER "${PROJECT_NAME}/framework")

project_install_and_export_targets(${PROJECT_SERVER_FRAME_LIB_LINK})
