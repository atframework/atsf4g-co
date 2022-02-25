﻿# 默认配置选项
#####################################################################

option(ATFRAMEWORK_USE_DYNAMIC_LIBRARY "Build and linking with dynamic libraries." ON)

# 编译的组件
option(PROJECT_ENABLE_SAMPLE "Enable build sample." OFF)
option(PROJECT_ENABLE_UNITTEST "Enable build unit test." OFF)
if(UNIX AND NOT CYGWIN)
  option(LIBUNWIND_ENABLED "Enable libunwind." ON)
endif()
option(PROJECT_TOOL_REPORT_COMPILE_UNIT_TIME "Show compiling time of each unit" OFF)
option(PROJECT_TOOL_REPORT_LINK_UNIT_TIME "Show linking time of each target." OFF)

# project name
set(PROJECT_BUILD_NAME
    "publish"
    CACHE STRING "Project name")
set(PROJECT_RPC_DB_BUFFER_LENGTH
    262144
    CACHE STRING "DB package buffer length, used in DB TLS buffer")

# just like ATBUS_MACRO_DATA_SMALL_SIZE
set(ATFRAME_GATEWAY_MACRO_DATA_SMALL_SIZE
    3072
    CACHE STRING
          "small message buffer for atgateway connection(used to reduce memory copy when there are many small messages)"
)

# Patch for `FindGit.cmake` on windows
find_program(GIT_EXECUTABLE NAMES git git.cmd)
find_package(Git)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} config remote.origin.url
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE PROJECT_GIT_REPO_URL)
  mark_as_advanced(FORCE PROJECT_GIT_REPO_URL)
  if(PROJECT_GIT_REPO_URL MATCHES "^(http:)|(https:)")
    option(PROJECT_GIT_REMOTE_ORIGIN_USE_SSH "Using ssh git url" OFF)
  else()
    option(PROJECT_GIT_REMOTE_ORIGIN_USE_SSH "Using ssh git url" ON)
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -n 1 --format="%cd %H" --encoding=UTF-8
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE SERVER_FRAME_VCS_COMMIT)

  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -n 1 "--format=%H" --encoding=UTF-8
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE SERVER_FRAME_VCS_COMMIT_SHA)

  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -n 1 --pretty=oneline --encoding=UTF-8
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE SERVER_FRAME_VCS_VERSION)

  execute_process(
    COMMAND ${GIT_EXECUTABLE} branch --show-current
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE SERVER_FRAME_VCS_SERVER_BRANCH)

  string(STRIP "${SERVER_FRAME_VCS_COMMIT}" SERVER_FRAME_VCS_COMMIT)
  string(STRIP "${SERVER_FRAME_VCS_VERSION}" SERVER_FRAME_VCS_VERSION)
  string(STRIP "${SERVER_FRAME_VCS_SERVER_BRANCH}" SERVER_FRAME_VCS_SERVER_BRANCH)
  string(REGEX REPLACE "[\r\n]" " " SERVER_FRAME_VCS_VERSION "${SERVER_FRAME_VCS_VERSION}")
  string(TIMESTAMP SERVER_FRAME_PROJECT_CONFIGURE_TIME "%Y%m%d%H%M")
  set(SERVER_FRAME_PROJECT_CONFIGURE_VERSION
      "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${SERVER_FRAME_PROJECT_CONFIGURE_TIME}"
  )
endif()

if(NOT SERVER_FRAME_VCS_COMMIT_SHA)
  set(SERVER_FRAME_VCS_COMMIT_SHA "UNKNOWN_COMMIT_SHA")
  set(SERVER_FRAME_VCS_COMMIT_SHORT_SHA "UNKNOWN_COMMIT")
else()
  string(STRIP "${SERVER_FRAME_VCS_COMMIT_SHA}" SERVER_FRAME_VCS_COMMIT_SHA)
  if(GIT_FOUND)
    foreach(SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH "8" "12" "16" "24" "32")
      string(SUBSTRING ${SERVER_FRAME_VCS_COMMIT_SHA} 0 ${SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH}
                       SERVER_FRAME_VCS_COMMIT_SHORT_SHA)
      execute_process(
        COMMAND ${GIT_EXECUTABLE} show "--format=%H" --encoding=UTF-8 ${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        RESULT_VARIABLE SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT
        OUTPUT_QUIET ERROR_QUIET)
      if(${SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT} EQUAL 0)
        break()
      endif()
    endforeach()
    if(NOT ${SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT} EQUAL 0)
      set(SERVER_FRAME_VCS_COMMIT_SHORT_SHA ${SERVER_FRAME_VCS_COMMIT_SHA})
    endif()
    unset(SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH)
    unset(SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT)
    message(STATUS "SERVER_FRAME_VCS_COMMIT_SHORT_SHA: ${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}")
  else()
    set(SERVER_FRAME_VCS_COMMIT_SHORT_SHA ${SERVER_FRAME_VCS_COMMIT_SHA})
  endif()
endif()

if(NOT NINJA_LINK_POOL_SIZE)
  set(NINJA_LINK_POOL_SIZE 6)
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS "PROJECT_${PROJECT_BUILD_NAME}_LINK_JOB_POOL=${NINJA_LINK_POOL_SIZE}")
set(CMAKE_JOB_POOL_LINK "PROJECT_${PROJECT_BUILD_NAME}_LINK_JOB_POOL")

# ============ fetch cmake toolset ============
include("${CMAKE_CURRENT_LIST_DIR}/FetchDependeny.cmake")
include(IncludeDirectoryRecurse)
include(EchoWithColor)

# Set output PATH
if(NOT PROJECT_INSTALL_BAS_DIR)
  set(PROJECT_INSTALL_BAS_DIR "${PROJECT_BINARY_DIR}/${PROJECT_BUILD_NAME}")
endif()
file(MAKE_DIRECTORY ${PROJECT_INSTALL_BAS_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_BINDIR}")
file(MAKE_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# Try to use mold
macro(project_try_set_linker)
  set(optionArgs "")
  set(oneValueArgs LEGACY_LINKER_DIRECTORY)
  set(multiValueArgs LINKER)
  cmake_parse_arguments(project_try_set_linker "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "AppleClang|Clang|GNU")
    return()
  endif()

  include(CheckCSourceCompiles)
  set(project_try_set_linker_SUCCESS FALSE)
  set(project_try_set_linker_BACKUP_CMAKE_REQUIRED_LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})

  foreach(project_try_set_linker_TRY_LINKER ${project_try_set_linker_LINKER})
    if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "12.0.0")
       OR (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "13.0.0"))
      set(project_try_set_linker_LINK_OPTION "--ld-path=${project_try_set_linker_TRY_LINKER}")
    else()
      set(project_try_set_linker_LINK_OPTION "-fuse-ld=${project_try_set_linker_TRY_LINKER}")
    endif()
    set(CMAKE_REQUIRED_LINK_OPTIONS ${project_try_set_linker_BACKUP_CMAKE_REQUIRED_LINK_OPTIONS}
                                    "${project_try_set_linker_LINK_OPTION}")
    message(STATUS "Test linker: ${project_try_set_linker_LINK_OPTION}")
    check_c_source_compiles("int main() { return 0; }" project_try_set_linker_test_compiling)
    if(project_try_set_linker_test_compiling)
      unset(project_try_set_linker_test_compiling CACHE)
      set(project_try_set_linker_SUCCESS TRUE)
      add_linker_flags_for_runtime_unique("${project_try_set_linker_LINK_OPTION}")
      message(STATUS "Use linker: ${project_try_set_linker_LINK_OPTION}")
      break()
    endif()
    unset(project_try_set_linker_test_compiling CACHE)
  endforeach()

  if(NOT project_try_set_linker_SUCCESS AND project_try_set_linker_LEGACY_LINKER_DIRECTORY)
    set(project_try_set_linker_LINK_OPTION "-B${project_try_set_linker_LEGACY_LINKER_DIRECTORY}")
    set(CMAKE_REQUIRED_LINK_OPTIONS ${project_try_set_linker_BACKUP_CMAKE_REQUIRED_LINK_OPTIONS}
                                    "${project_try_set_linker_LINK_OPTION}")
    message(STATUS "Test linker directory: ${project_try_set_linker_LINK_OPTION}")
    check_c_source_compiles("int main() { return 0; }" project_try_set_linker_test_compiling)
    if(project_try_set_linker_test_compiling)
      unset(project_try_set_linker_test_compiling CACHE)
      set(project_try_set_linker_SUCCESS TRUE)
      add_linker_flags_for_runtime_unique("${project_try_set_linker_LINK_OPTION}")
      message(STATUS "Use linker: ${project_try_set_linker_LINK_OPTION}")
    endif()
  endif()

  set(CMAKE_REQUIRED_LINK_OPTIONS ${project_try_set_linker_BACKUP_CMAKE_REQUIRED_LINK_OPTIONS})
  unset(project_try_set_linker_TRY_LINKER)
  unset(project_try_set_linker_LINK_OPTION)
  unset(project_try_set_linker_SUCCESS)
  unset(project_try_set_linker_BACKUP_CMAKE_REQUIRED_LINK_OPTIONS)
endmacro()

find_program(PROJECT_FIND_LINKER_MOLD NAMES mold mold.exe)
if(PROJECT_FIND_LINKER_MOLD)
  get_filename_component(PROJECT_FIND_LINKER_MOLD_PREFIX_DIR "${PROJECT_FIND_LINKER_MOLD}" DIRECTORY)
  get_filename_component(PROJECT_FIND_LINKER_MOLD_PREFIX_DIR "${PROJECT_FIND_LINKER_MOLD_PREFIX_DIR}" DIRECTORY)
  if(EXISTS "${PROJECT_FIND_LINKER_MOLD_PREFIX_DIR}/libexec/mold/ld")
    project_try_set_linker(LINKER "${PROJECT_FIND_LINKER_MOLD}" LEGACY_LINKER_DIRECTORY
                           "${PROJECT_FIND_LINKER_MOLD_PREFIX_DIR}/libexec/mold")
  else()
    project_try_set_linker(LINKER "${PROJECT_FIND_LINKER_MOLD}")
  endif()
endif()

if(PROJECT_TOOL_REPORT_COMPILE_UNIT_TIME)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
endif()
if(PROJECT_TOOL_REPORT_LINK_UNIT_TIME)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")
endif()
