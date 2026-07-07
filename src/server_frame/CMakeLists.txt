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

# add generated files
include("${PROJECT_SERVER_FRAME_BAS_DIR}/generate_proto_source.cmake")
include("${PROJECT_SERVER_FRAME_BAS_DIR}/generate_config_codes.cmake")

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.RouterService"
  "${CMAKE_CURRENT_LIST_DIR}"
  TASK_PATH_PREFIX
  "router"
  HANDLE_PATH_PREFIX
  "router"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  SERVER_FRAME_API
  RPC_DLLEXPORT_DECL
  SERVER_FRAME_API)

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.LogicCommonService"
  "${CMAKE_CURRENT_LIST_DIR}"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "logic"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  SERVER_FRAME_API
  RPC_DLLEXPORT_DECL
  SERVER_FRAME_API)

generate_for_pb_add_custom_configure(
  "db_code_generate_local"
  RULE_BODY_HEAD
  "
  - file:
      overwrite: false
      output_directory: '${GENERATE_FOR_PB_SOURCE_DIR}/server_frame/rpc/db/'
      custom_variables:
        generate_proto_file: 'protocol/pbdesc/svr.local.table.proto'
        include_cpp: 'local_db_interface.h'
"
  OVERWRITE_RULE
  "
      file_template:
        - overwrite: true
          input: '${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.cpp.mako'
          output: 'local_db_interface.cpp'
        - overwrite: true
          input: '${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.h.mako'
          output: 'local_db_interface.h'
"
  PROTOCOL_TARGET
  "${SERVER_FRAME_BASE_PROTOCOLS}"
  TEMPLATE_DEPENDS
  "${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.cpp.mako"
  "${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.h.mako"
)
generate_for_pb_add_custom_configure(
  "db_code_generate_global"
  RULE_BODY_HEAD
  "
  - file:
      overwrite: false
      output_directory: '${GENERATE_FOR_PB_SOURCE_DIR}/server_frame/rpc/db/'
      custom_variables:
        generate_proto_file: 'protocol/pbdesc/svr.global.table.proto'
        include_cpp: 'global_db_interface.h'
"
  OVERWRITE_RULE
  "
      file_template:
        - overwrite: true
          input: '${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.cpp.mako'
          output: 'global_db_interface.cpp'
        - overwrite: true
          input: '${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.h.mako'
          output: 'global_db_interface.h'
"
  PROTOCOL_TARGET
  "${SERVER_FRAME_BASE_PROTOCOLS}"
  TEMPLATE_DEPENDS
  "${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.cpp.mako"
  "${PROJECT_SOURCE_TEMPLATE_DIR}/db_interface.h.mako"
)

list(APPEND PROJECT_SERVER_FRAME_GENERATED_FLOW_NAMES
    "${PROJECT_NAMESPACE}.RouterService"
    "${PROJECT_NAMESPACE}.LogicCommonService"
    "db_code_generate_local"
    "db_code_generate_global")
generate_for_pb_collect_output_from_flows("${PROJECT_SERVER_FRAME_GENERATED_FLOW_NAMES}" _PROJECT_COMPONENT_GENERATED_OUTPUT_FILES)
generate_for_pb_collect_filtered_output_files(
  "${CMAKE_CURRENT_LIST_DIR}" "${_PROJECT_COMPONENT_GENERATED_OUTPUT_FILES}"
  _PROJECT_COMPONENT_GENERATED_SOURCE_FILES _PROJECT_COMPONENT_GENERATED_HEADER_FILES)
if(_PROJECT_COMPONENT_GENERATED_SOURCE_FILES)
  list(APPEND SRC_LIST ${_PROJECT_COMPONENT_GENERATED_SOURCE_FILES})
endif()
if(_PROJECT_COMPONENT_GENERATED_HEADER_FILES)
  list(APPEND SRC_LIST ${_PROJECT_COMPONENT_GENERATED_HEADER_FILES})
endif()

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
generate_for_pb_add_dependencies(${PROJECT_SERVER_FRAME_LIB_LINK} "${PROJECT_SERVER_FRAME_GENERATED_FLOW_NAMES}")
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