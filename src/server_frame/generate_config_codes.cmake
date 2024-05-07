# Python3_EXECUTABLE
# PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_PY
unset(PROJECT_SERVER_FRAME_CONFIG_SRC_LIST)

set(PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/_generated/config/include/config")
set(PROJECT_SERVER_FRAME_CONFIG_GENERATED_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_generated/config/src")
set(PROJECT_SERVER_FRAME_CONFIG_GENERATED_TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/_generated/config/temp")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR}/excel")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_SOURCE_DIR}")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_TEMP_DIR}")

execute_process(
  COMMAND
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" --proto_path
    "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}" --proto_path "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_CONFIG_DIR}"
    --proto_path "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}" --proto_path "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
    --proto_path "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include" -o "${CMAKE_CURRENT_BINARY_DIR}/config-test.pb"
    # Protocol buffer files
    ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_COMMON} ${PROJECT_SERVER_FRAME_PROTO_SANDBOX_LIST_CONFIG}
    "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe/atapp_conf.proto"
    "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include/libatbus_protocol.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/any.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/empty.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/duration.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/timestamp.proto"
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/descriptor.proto"
  RESULT_VARIABLE PROJECT_SERVER_FRAME_CONFIG_SET_RES
  ERROR_VARIABLE PROJECT_SERVER_FRAME_CONFIG_SET_ERR COMMAND_ECHO STDOUT)

if(NOT ${PROJECT_SERVER_FRAME_CONFIG_SET_RES} EQUAL 0)
  message(FATAL_ERROR "${PROJECT_SERVER_FRAME_CONFIG_SET_ERR}")
endif()

execute_process(
  COMMAND
    ${Python3_EXECUTABLE} ${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_PY} --add-package-prefix
    "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}" -i "${PROJECT_SOURCE_TEMPLATE_DIR}" -p
    "${CMAKE_CURRENT_BINARY_DIR}/config-test.pb" -o "${CMAKE_CURRENT_LIST_DIR}/config" -g
    "${PROJECT_SOURCE_TEMPLATE_DIR}/config_manager.h.mako:include/config/excel/config_manager.h" -g
    "${PROJECT_SOURCE_TEMPLATE_DIR}/config_manager.cpp.mako:src/excel/config_manager.cpp" -l
    "H:${PROJECT_SOURCE_TEMPLATE_DIR}/config_set.h.mako:include/config/excel/\${loader.get_cpp_header_path()}" -l
    "S:${PROJECT_SOURCE_TEMPLATE_DIR}/config_set.cpp.mako:src/excel/\${loader.get_cpp_source_path()}" -g
    "${PROJECT_SOURCE_TEMPLATE_DIR}/config_easy_api.h.mako:include/config/excel/config_easy_api.h" -g
    "${PROJECT_SOURCE_TEMPLATE_DIR}/config_easy_api.cpp.mako:src/excel/config_easy_api.cpp" --pb-include-prefix
    "protocol/config/" --print-output-file
  OUTPUT_VARIABLE PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST_STR
  RESULT_VARIABLE PROJECT_SERVER_FRAME_CONFIG_SET_RES
  ERROR_VARIABLE PROJECT_SERVER_FRAME_CONFIG_SET_ERR COMMAND_ECHO STDOUT)

if(NOT ${PROJECT_SERVER_FRAME_CONFIG_SET_RES} EQUAL 0)
  message(FATAL_ERROR "${PROJECT_SERVER_FRAME_CONFIG_SET_ERR}")
endif()
unset(PROJECT_SERVER_FRAME_CONFIG_SET_RES)
unset(PROJECT_SERVER_FRAME_CONFIG_SET_ERR)

unset(PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST)
string(REPLACE "\\" "/" PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST_STR "${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST_STR}")
string(REGEX MATCHALL "[^\r\n]+" PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST
             ${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST_STR})

set(PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/_generated/config/temp")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SRC_DIR}")
unset(PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_HEADER_LIST)
unset(PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SOURCE_LIST)
foreach(SRC_ITEM ${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST})
  get_filename_component(CONFIG_SET_SRC_BASENAME "${SRC_ITEM}" NAME)
  if(CONFIG_SET_SRC_BASENAME MATCHES "\\.h$")
    list(APPEND PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_HEADER_LIST
         "${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SRC_DIR}/${CONFIG_SET_SRC_BASENAME}")
  else()
    list(APPEND PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SOURCE_LIST
         "${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SRC_DIR}/${CONFIG_SET_SRC_BASENAME}")
  endif()
endforeach()

if(CMAKE_HOST_WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
  set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT
      "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-config-loader.ps1")
  set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER
      "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" -File "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}")
  generate_for_pb_initialize_pwsh("${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}")
else()
  set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT
      "${CMAKE_CURRENT_BINARY_DIR}/generate-excel-config-loader.sh")
  set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER
      "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}")
  generate_for_pb_initialize_sh("${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}")
endif()

set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "'${Python3_EXECUTABLE}' '${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_PY}' '--add-package-prefix' '${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-i' '${PROJECT_SOURCE_TEMPLATE_DIR}'")

set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-p' '${PROJECT_GENERATED_PBD_DIR}/config.pb'")
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-o' '${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SRC_DIR}'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '--set' 'cpp_include_prefix=config/excel/'")
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '--set' 'xresloader_include_prefix=protocol/config/'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-g' 'config_manager.h.mako:config_manager.h'")
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-g' 'config_manager.cpp.mako:config_manager.cpp'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-l' 'H:config_set.h.mako:\${loader.get_cpp_header_path()}'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-l' 'S:config_set.cpp.mako:\${loader.get_cpp_source_path()}'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-g' 'config_easy_api.h.mako:config_easy_api.h'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-g' 'config_easy_api.cpp.mako:config_easy_api.cpp'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-c' 'custom_config_group:custom_group_fields.h.mako'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-c' 'custom_config_include:custom_include_fields.h.mako'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '-c' 'custom_config_easy_api_include:custom_easy_api_include_fields.h.mako'"
)
set(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND} '--pb-include-prefix' 'protocol/config/'")
if(CMAKE_HOST_WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
  file(
    APPEND "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}"
    "& ${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  file(
    APPEND "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}"
    "& \"${CMAKE_COMMAND}\" -E copy_if_different \"${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/template/common/cpp/config/excel/config_traits.h\" \"${PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR}/excel/config_traits.h\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  )
else()
  file(APPEND "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}"
       "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  file(
    APPEND "${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT}"
    "\"${CMAKE_COMMAND}\" -E copy_if_different \"${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/template/common/cpp/config/excel/config_traits.h\" \"${PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR}/excel/config_traits.h\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
  )
endif()
list(APPEND PROJECT_SERVER_FRAME_CONFIG_SRC_LIST
     "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR}/excel/config_traits.h")
unset(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_COMMAND)
unset(PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER_SCRIPT)

add_custom_command(
  OUTPUT ${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST}
         "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR}/excel/config_traits.h"
  COMMAND "${CMAKE_COMMAND}" -E remove -f ${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_HEADER_LIST}
          ${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SOURCE_LIST}
  COMMAND ${PROJECT_SERVER_FRAME_CONFIG_GENERATE_EXCEL_CONFIG_LOADER}
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_HEADER_LIST}
          "${CMAKE_CURRENT_LIST_DIR}/config/include/config/excel/"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SOURCE_LIST}
          "${CMAKE_CURRENT_LIST_DIR}/config/src/excel/"
  DEPENDS "${PROJECT_GENERATED_PBD_DIR}/config.pb"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/config_manager.h.mako"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/config_manager.cpp.mako"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/custom_group_fields.h.mako"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/custom_include_fields.h.mako"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/config_set.h.mako"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/config_set.cpp.mako"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/config_easy_api.h.mako"
          "${PROJECT_SOURCE_TEMPLATE_DIR}/config_easy_api.cpp.mako"
  COMMENT "Generate ${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST}")

list(APPEND PROJECT_SERVER_FRAME_CONFIG_SRC_LIST ${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST})

add_custom_target(
  config-loader
  DEPENDS ${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST}
  SOURCES ${PROJECT_SERVER_FRAME_CONFIG_SET_SRC_LIST})
if(MSVC)
  set_property(TARGET "config-loader" PROPERTY FOLDER "${PROJECT_NAME}")
endif()

unset(PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_HEADER_LIST)
unset(PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SOURCE_LIST)
unset(PROJECT_SERVER_FRAME_CONFIG_SET_GENERATED_SRC_DIR)

configure_file("${CMAKE_CURRENT_LIST_DIR}/config/include/config/server_frame_build_feature.h.in"
               "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_TEMP_DIR}/server_frame_build_feature.h" ESCAPE_QUOTES @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/config/src/server_frame_build_feature.cpp.in"
               "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_TEMP_DIR}/server_frame_build_feature.cpp" ESCAPE_QUOTES @ONLY)
list(APPEND PROJECT_SERVER_FRAME_CONFIG_SRC_LIST
     "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR}/server_frame_build_feature.h"
     "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_SOURCE_DIR}/server_frame_build_feature.cpp")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_TEMP_DIR}/server_frame_build_feature.h"
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR}/")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_TEMP_DIR}/server_frame_build_feature.cpp"
    "${PROJECT_SERVER_FRAME_CONFIG_GENERATED_SOURCE_DIR}/")
unset(PROJECT_SERVER_FRAME_CONFIG_GENERATED_HEADER_DIR)
unset(PROJECT_SERVER_FRAME_CONFIG_GENERATED_SOURCE_DIR)
unset(PROJECT_SERVER_FRAME_CONFIG_GENERATED_TEMP_DIR)

file(GLOB_RECURSE PROJECT_SERVER_FRAME_CONFIG_SRC_ORIGIN ${PROJECT_SERVER_FRAME_SRC_DIR}/config/include/config/*.h
     ${PROJECT_SERVER_FRAME_SRC_DIR}/config/src/*.cpp)

foreach(SRC_FILE_REALPATH IN LISTS PROJECT_SERVER_FRAME_CONFIG_SRC_ORIGIN)
  get_filename_component(SRC_DIRPATH_REALPATH ${SRC_FILE_REALPATH} DIRECTORY)
  if(SRC_FILE_REALPATH
     MATCHES
     "^${PROJECT_SERVER_FRAME_SRC_DIR}[/\\\\]config[/\\\\](src|include[/\\\\]config)[/\\\\]server_frame_build_feature.(h|cpp)"
  )
    continue()
  endif()
  if(SRC_FILE_REALPATH MATCHES
     "^${PROJECT_SERVER_FRAME_SRC_DIR}[/\\\\]config[/\\\\](src|include[/\\\\]config)[/\\\\]excel[/\\\\]")
    continue()
  endif()
  list(APPEND PROJECT_SERVER_FRAME_CONFIG_SRC_LIST ${SRC_FILE_REALPATH})
endforeach()
unset(SRC_FILE_REALPATH)
unset(SRC_DIRPATH_REALPATH)
unset(PROJECT_SERVER_FRAME_CONFIG_SRC_ORIGIN)

source_group_by_dir(PROJECT_SERVER_FRAME_CONFIG_SRC_LIST)
if(BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK}-config SHARED ${PROJECT_SERVER_FRAME_CONFIG_SRC_LIST})
  set_target_properties(
    ${PROJECT_SERVER_FRAME_LIB_LINK}-config
    PROPERTIES
      VERSION "${PROJECT_VERSION}"
      SOVERSION "${PROJECT_VERSION}"
      INSTALL_RPATH
      "${PROJECT_RPATH_ORIGIN};${PROJECT_RPATH_ORIGIN}/../../${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${PROJECT_SERVER_FRAME_LIB_LINK}/${CMAKE_INSTALL_LIBDIR};${PROJECT_INSTALL_RPATH}"
  )

  project_tool_split_target_debug_sybmol(${PROJECT_SERVER_FRAME_LIB_LINK}-config)
  target_compile_definitions("${PROJECT_SERVER_FRAME_LIB_LINK}-config" PRIVATE SERVER_FRAME_CONFIG_NATIVE=1
                                                                               SERVER_FRAME_CONFIG_DLL=1)
else()
  add_library(${PROJECT_SERVER_FRAME_LIB_LINK}-config STATIC ${PROJECT_SERVER_FRAME_CONFIG_SRC_LIST})
  set_target_properties(${PROJECT_SERVER_FRAME_LIB_LINK}-config PROPERTIES VERSION "${PROJECT_VERSION}")
  target_compile_definitions("${PROJECT_SERVER_FRAME_LIB_LINK}-config" PRIVATE SERVER_FRAME_CONFIG_NATIVE=1)
endif()

set_target_properties(
  ${PROJECT_SERVER_FRAME_LIB_LINK}-config
  PROPERTIES C_VISIBILITY_PRESET "hidden"
             CXX_VISIBILITY_PRESET "hidden"
             VERSION "${PROJECT_VERSION}"
             BUILD_RPATH_USE_ORIGIN YES
             PORJECT_PROTOCOL_DIR "${PROTOCOL_DIR}")

if(MSVC)
  set_property(TARGET "${PROJECT_SERVER_FRAME_LIB_LINK}-config" PROPERTY FOLDER "${PROJECT_NAME}")
endif()

target_include_directories(
  ${PROJECT_SERVER_FRAME_LIB_LINK}-config
  PUBLIC "$<BUILD_INTERFACE:${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include>"
         "$<BUILD_INTERFACE:${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include>"
         "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/config/include>"
         "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/_generated/config/include>"
         "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_options(${PROJECT_SERVER_FRAME_LIB_LINK}-config PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
  target_link_options(${PROJECT_SERVER_FRAME_LIB_LINK}-config PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
endif()

add_dependencies(${PROJECT_SERVER_FRAME_LIB_LINK}-config protocol config-loader)
target_link_libraries(${PROJECT_SERVER_FRAME_LIB_LINK}-config PUBLIC ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME}
                                                                     ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol)

install(
  TARGETS ${PROJECT_SERVER_FRAME_LIB_LINK}-config
  EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_generated/config/include/config/server_frame_build_feature.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/config/")
