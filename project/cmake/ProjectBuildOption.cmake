include(CMakeDependentOption)

# 默认配置选项
# ######################################################################################################################

# gRPC在macOS上符号可见性设置有兼容性问题
if(UNIX AND NOT APPLE)
  option(ATFRAMEWORK_USE_DYNAMIC_LIBRARY "Build and linking with dynamic libraries." ON)
else()
  option(ATFRAMEWORK_USE_DYNAMIC_LIBRARY "Build and linking with dynamic libraries." OFF)
endif()

# 编译的组件
option(PROJECT_ENABLE_SAMPLE "Enable build sample." OFF)
option(PROJECT_ENABLE_UNITTEST "Enable build unit test." OFF)
option(PROJECT_ENABLE_PRECOMPILE_HEADERS "Enable precompile headers." ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "12.1.0")
  option(PROJECT_ENABLE_LINKER_MOLD "Enable use mold as linker." ON)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
  option(PROJECT_ENABLE_LINKER_MOLD "Enable use mold as linker." ON)
else()
  option(PROJECT_ENABLE_LINKER_MOLD "Enable use mold as linker." OFF)
endif()
option(PROJECT_ENABLE_SPLIT_DEBUG_INFORMATION "Enable split debug information." OFF)
option(PROJECT_ENABLE_COMPRESS_DEBUG_INFORMATION "Enable compress debug information." OFF)

option(PROJECT_TOOL_REPORT_COMPILE_UNIT_TIME "Show compiling time of each unit" OFF)
option(PROJECT_TOOL_REPORT_LINK_UNIT_TIME "Show linking time of each target." OFF)
set(PROJECT_TOOL_ENABLE_SPLIT_DEBUG_SYMBOL_SUFFIX
    ".dbg"
    CACHE STRING "Split debug symbols into standalone files.")

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

option(PROJECT_SANTIZER_USE_ADDRESS "Enable -fsanitize=address -fno-omit-frame-pointer" OFF)
option(PROJECT_SANTIZER_USE_THREAD "Enable -fsanitize=thread -fno-omit-frame-pointer -DTHREAD_SANITIZER" OFF)
option(PROJECT_SANTIZER_USE_LEAK "Enable -fsanitize=leak -fno-omit-frame-pointer" OFF)
option(PROJECT_SANTIZER_USE_UNDEFINED "Enable -fsanitize=undefined -fno-omit-frame-pointer" OFF)
option(PROJECT_SANTIZER_USE_HWADDRESS "Enable -fsanitize=hwaddress -fno-omit-frame-pointer" OFF)
option(PROJECT_STATIC_LINK_STANDARD_LIBRARIES "Use -static-libgcc and -static-libstdc++" OFF)
option(PROJECT_OPTIMIZE_OPTIONS_NO_OMIT_FRAME_POINTER "Use -fno-omit-frame-pointer" ON)

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
  if(NOT SERVER_FRAME_PROJECT_USER_BUSSINESS_VERSION)
    set(SERVER_FRAME_PROJECT_USER_BUSSINESS_VERSION "0.0.0.0")
  endif()
endif()
if(NOT DEFINED PROJECT_GITHUB_DOWNLOAD_MIRROR)
  if(DEFINED ENV{PROJECT_GITHUB_DOWNLOAD_MIRROR})
    set(PROJECT_GITHUB_DOWNLOAD_MIRROR "$ENV{PROJECT_GITHUB_DOWNLOAD_MIRROR}")
  endif()
endif()
if(NOT DEFINED PROJECT_GITHUB_DOWNLOAD_MIRROR)
  # set(PROJECT_GITHUB_DOWNLOAD_MIRROR "https://mirrors.tencent.com/github.com/")
  set(PROJECT_GITHUB_DOWNLOAD_MIRROR "https://github.com/")
endif()
if(NOT DEFINED PROJECT_GITHUB_GIT_HTTP_MIRROR)
  if(DEFINED ENV{PROJECT_GITHUB_GIT_HTTP_MIRROR})
    set(PROJECT_GITHUB_GIT_HTTP_MIRROR "$ENV{PROJECT_GITHUB_GIT_MIRROR}")
  endif()
endif()
if(NOT DEFINED PROJECT_GITHUB_GIT_HTTP_MIRROR)
  # set(PROJECT_GITHUB_GIT_HTTP_MIRROR "https://mirrors.tencent.com/github.com/")
  set(PROJECT_GITHUB_GIT_HTTP_MIRROR "https://github.com/")
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

# Options depend compile environments must be added after fetch cmake toolset
# option(PROJECT_SERVER_FRAME_USE_STD_COROUTINE "Using C++20 Coroutine" OFF)
cmake_dependent_option(PROJECT_SERVER_FRAME_USE_STD_COROUTINE "Using C++20 Coroutine" ON
                       "COMPILER_OPTIONS_TEST_STD_COROUTINE" OFF)
option(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT "Enable await checker for legacy coroutine" ON)

if(NOT DEFINED PROJECT_SERVER_FRAME_ENABLE_RPC_MOCK)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(PROJECT_SERVER_FRAME_ENABLE_RPC_MOCK "Enable RPC mock APIs" ON)
  else()
    option(PROJECT_SERVER_FRAME_ENABLE_RPC_MOCK "Enable RPC mock APIs" OFF)
  endif()
endif()

# Set output PATH
if(NOT PROJECT_INSTALL_BAS_DIR)
  set(PROJECT_INSTALL_BAS_DIR "${PROJECT_BINARY_DIR}/${PROJECT_BUILD_NAME}")
endif()
file(MAKE_DIRECTORY ${PROJECT_INSTALL_BAS_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_BINDIR}")
file(MAKE_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# 导入工程工具
include("${CMAKE_CURRENT_LIST_DIR}/ProjectTools.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ProjectSantizerChecker.cmake")

# 导入第三方库工具
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/Configure.cmake")

# Linker options
unset(PROJECT_TRY_SET_LINKER)
if(PROJECT_ENABLE_LINKER_MOLD)
  find_program(PROJECT_FIND_LINKER_MOLD NAMES mold mold.exe)
  if(PROJECT_FIND_LINKER_MOLD)
    get_filename_component(PROJECT_FIND_LINKER_MOLD_PREFIX_DIR "${PROJECT_FIND_LINKER_MOLD}" DIRECTORY)
    get_filename_component(PROJECT_FIND_LINKER_MOLD_PREFIX_DIR "${PROJECT_FIND_LINKER_MOLD_PREFIX_DIR}" DIRECTORY)
    if(EXISTS "${PROJECT_FIND_LINKER_MOLD_PREFIX_DIR}/libexec/mold/ld")
      try_set_linker(LINKER "${PROJECT_FIND_LINKER_MOLD}" LEGACY_LINKER_DIRECTORY
                     "${PROJECT_FIND_LINKER_MOLD_PREFIX_DIR}/libexec/mold")
    else()
      try_set_linker(LINKER "${PROJECT_FIND_LINKER_MOLD}")
    endif()
    set(PROJECT_TRY_SET_LINKER "${PROJECT_FIND_LINKER_MOLD}")
    # Some version of mold will have problems when linking objects, just ignore them.
    #[[
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      add_linker_flags_for_runtime_unique(-Wl,--unresolved-symbols,ignore-in-object-files)
    endif()
    ]]
  endif()
endif()

#[[
# ld.gold is deprecated from binutils 2.44 and is removed in the latest version
if(NOT PROJECT_TRY_SET_LINKER
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # ld.gold is deprecated from binutils 2.44 and is removed in the latest version
  try_set_linker(LINKER gold)
  set(PROJECT_TRY_SET_LINKER "gold")
endif()
]]

if(PROJECT_TOOL_REPORT_COMPILE_UNIT_TIME)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
endif()
if(PROJECT_TOOL_REPORT_LINK_UNIT_TIME)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")
endif()
