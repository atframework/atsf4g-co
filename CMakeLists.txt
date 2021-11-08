cmake_minimum_required(VERSION 3.16.0)
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0022 NEW)
cmake_policy(SET CMP0067 NEW)
cmake_policy(SET CMP0074 NEW)
cmake_policy(SET CMP0091 NEW)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.19.0")
  cmake_policy(SET CMP0111 NEW)
endif()

project(
  atf4g-co
  VERSION "0.10.0"
  HOMEPAGE_URL "https://github.com/atframework/atsf4g-co"
  LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/project/cmake/ProjectBuildOption.cmake")
include(MaybePopulateSubmodule)
include("${CMAKE_CURRENT_LIST_DIR}/third_party/Repository.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/atframework/Repository.cmake")

echowithcolor(COLOR GREEN "-- Build Type: ${CMAKE_BUILD_TYPE}")

# 导入工程工具
include("${CMAKE_CURRENT_LIST_DIR}/project/cmake/ProjectTools.cmake")

# installer directory
if(NOT PROJECT_INSTALL_BAS_DIR)
  set(PROJECT_INSTALL_BAS_DIR "${PROJECT_BINARY_DIR}/${PROJECT_BUILD_NAME}")
endif()
if(NOT EXISTS ${PROJECT_INSTALL_BAS_DIR})
  file(MAKE_DIRECTORY ${PROJECT_INSTALL_BAS_DIR})
  message(STATUS "create ${PROJECT_INSTALL_BAS_DIR} for build target.")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}/${PROJECT_VCS_COMMIT_SHORT_SHA}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}/${PROJECT_VCS_COMMIT_SHORT_SHA}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_BINDIR}")

set(PROJECT_INSTALL_RES_DIR "${PROJECT_INSTALL_BAS_DIR}/resource")
set(PROJECT_INSTALL_TOOLS_DIR "${PROJECT_INSTALL_BAS_DIR}/tools")
set(PROJECT_INSTALL_SHARED_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
set(PROJECT_INSTALL_RES_PBD_DIR "${PROJECT_INSTALL_RES_DIR}/pbdesc")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_TOOLS_DIR}/bin")

file(MAKE_DIRECTORY ${PROJECT_INSTALL_SHARED_DIR})
file(MAKE_DIRECTORY ${PROJECT_INSTALL_RES_PBD_DIR})

# ######################################################################################################################
if(MINGW)
  if("" STREQUAL "$ENV{MINGW_MOUNT_POINT}")
    if(NOT "" STREQUAL "$ENV{MINGW_PREFIX}")
      set(ENV{MINGW_MOUNT_POINT} $ENV{MINGW_PREFIX})
    else()
      message(FATAL_ERROR "You must set environment MINGW_PREFIX or MINGW_MOUNT_POINT when using mingw")
    endif()
  endif()

  list(APPEND CMAKE_SYSTEM_LIBRARY_PATH "$ENV{MINGW_MOUNT_POINT}/lib" "$ENV{MINGW_MOUNT_POINT}/libexec")
  list(APPEND CMAKE_SYSTEM_INCLUDE_PATH "$ENV{MINGW_MOUNT_POINT}/include")
  list(APPEND CMAKE_SYSTEM_PROGRAM_PATH "$ENV{MINGW_MOUNT_POINT}/bin" "$ENV{MINGW_MOUNT_POINT}/libexec")
  message(STATUS "MinGW => CMAKE_LIBRARY_PATH=${CMAKE_LIBRARY_PATH}")
  message(STATUS "MinGW => CMAKE_SYSTEM_LIBRARY_PATH=${CMAKE_SYSTEM_LIBRARY_PATH}")
  message(STATUS "MinGW => CMAKE_SYSTEM_INCLUDE_PATH=${CMAKE_SYSTEM_INCLUDE_PATH}")
  message(STATUS "MinGW => CMAKE_INCLUDE_PATH=${CMAKE_INCLUDE_PATH}")
  message(STATUS "MinGW => CMAKE_SYSTEM_FRAMEWORK_PATH=${CMAKE_SYSTEM_FRAMEWORK_PATH}")
  message(STATUS "MinGW => CMAKE_PROGRAM_PATH=${CMAKE_PROGRAM_PATH}")
  message(STATUS "MinGW => CMAKE_SYSTEM_PROGRAM_PATH=${CMAKE_SYSTEM_PROGRAM_PATH}")
endif()

# 增加相对链接路径
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH YES)
set(CMAKE_BUILD_WITH_INSTALL_RPATH NO)
set(CMAKE_BUILD_RPATH_USE_ORIGIN YES)

# 导入所有工程项目
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/atframework")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/src")

if(PROJECT_ENABLE_SAMPLE)
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/sample")
endif()

# copy 资源
file(GLOB PROJECT_INSTALL_DST_LIST "${CMAKE_CURRENT_LIST_DIR}/install/*")
foreach(INSTALL_DST_ITEM ${PROJECT_INSTALL_DST_LIST})
  echowithcolor(COLOR GREEN "-- Install: Copy ${INSTALL_DST_ITEM} ...")
  file(
    COPY "${INSTALL_DST_ITEM}"
    DESTINATION "${PROJECT_INSTALL_BAS_DIR}"
    USE_SOURCE_PERMISSIONS)
endforeach()

configure_file("${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.sh.in" "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.sh" ESCAPE_QUOTES
               @ONLY)

configure_file("${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.ps1.in" "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.ps1"
               ESCAPE_QUOTES @ONLY)

# add +rx permission
if(CYGWIN
   OR UNIX
   OR APPLE)
  execute_process(COMMAND chmod +rx -R "${PROJECT_INSTALL_BAS_DIR}" WORKING_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}")
endif()

if(CYGWIN
   OR UNIX
   OR APPLE)
  echowithcolor(COLOR GREEN "-- Cleanup old built libraries ...")
  execute_process(COMMAND "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.sh" WORKING_DIRECTORY ${PROJECT_INSTALL_BAS_DIR})
else()
  find_program(POWERSHELL_BIN NAMES pwsh pwsh.exe powershell powershell.exe)
  if(POWERSHELL_BIN)
    echowithcolor(COLOR GREEN "-- Cleanup old built libraries ...")
    execute_process(
      COMMAND ${POWERSHELL_BIN} "-NoProfile" "-InputFormat" "None" "-ExecutionPolicy" "Bypass" "-NonInteractive"
              "-NoLogo" "-File" "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.ps1"
      WORKING_DIRECTORY ${PROJECT_INSTALL_BAS_DIR})
  endif()
endif()

# 安装目录
install(
  DIRECTORY "${PROJECT_INSTALL_BAS_DIR}"
  DESTINATION "."
  USE_SOURCE_PERMISSIONS
  PATTERN ".svn" EXCLUDE)

export(
  EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
  NAMESPACE "${PROJECT_NAME}::"
  FILE ${PROJECT_INSTALL_EXPORT_FILE})
install(
  EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
  NAMESPACE "${PROJECT_NAME}::"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake")
