cmake_minimum_required(VERSION 3.16.0)
cmake_policy(SET CMP0022 NEW)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0067 NEW)
cmake_policy(SET CMP0074 NEW)
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0091 NEW)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.19.0")
  cmake_policy(SET CMP0111 NEW)
endif()

# MSVC RTTI flag /GR should not be not added to CMAKE_CXX_FLAGS by default. @see
# https://cmake.org/cmake/help/latest/policy/CMP0117.html
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.20.0")
  cmake_policy(SET CMP0117 NEW)
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.22.0")
  cmake_policy(SET CMP0128 NEW)
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.27.0")
  cmake_policy(SET CMP0144 NEW)
endif()

project(
  atf4g-co
  VERSION "0.11.0"
  HOMEPAGE_URL "https://github.com/atframework/atsf4g-co"
  LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/project/cmake/ProjectBuildOption.cmake")
include(MaybePopulateSubmodule)
include("${CMAKE_CURRENT_LIST_DIR}/third_party/Repository.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/atframework/Repository.cmake")

# installer directory
if(NOT PROJECT_INSTALL_BAS_DIR)
  set(PROJECT_INSTALL_BAS_DIR "${PROJECT_BINARY_DIR}/${PROJECT_BUILD_NAME}")
endif()
if(NOT EXISTS ${PROJECT_INSTALL_BAS_DIR})
  file(MAKE_DIRECTORY ${PROJECT_INSTALL_BAS_DIR})
  message(STATUS "create ${PROJECT_INSTALL_BAS_DIR} for build target.")
endif()

set(PROJECT_INSTALL_RES_DIR "${PROJECT_INSTALL_BAS_DIR}/resource")
set(PROJECT_INSTALL_TOOLS_DIR "${PROJECT_INSTALL_BAS_DIR}/tools")
set(PROJECT_INSTALL_SHARED_DIR "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
set(PROJECT_INSTALL_RES_PBD_DIR "${PROJECT_INSTALL_RES_DIR}/pbdesc")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_TOOLS_DIR}/bin")

file(MAKE_DIRECTORY ${PROJECT_INSTALL_SHARED_DIR})
file(MAKE_DIRECTORY ${PROJECT_INSTALL_RES_PBD_DIR})

set(PROJECT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/_generated")
set(PROJECT_GENERATED_PBD_DIR "${PROJECT_GENERATED_DIR}/pbdesc")
set(PROJECT_GENERATED_TOOLS_DIR "${PROJECT_GENERATED_DIR}/tools")
file(MAKE_DIRECTORY "${PROJECT_GENERATED_PBD_DIR}")
file(MAKE_DIRECTORY "${PROJECT_GENERATED_TOOLS_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/third_party/ConfigureInstall.cmake")

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
set(CMAKE_BUILD_WITH_INSTALL_RPATH YES)
set(CMAKE_BUILD_RPATH_USE_ORIGIN YES)

if(UNIX AND NOT APPLE)
  set(PROJECT_RPATH_ORIGIN "$ORIGIN")
elseif(APPLE)
  set(PROJECT_RPATH_ORIGIN "@loader_path")
endif()
set(PROJECT_INSTALL_RPATH
    "${PROJECT_RPATH_ORIGIN}/../../${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}"
    "${PROJECT_RPATH_ORIGIN}/../../../${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND CMAKE_INSTALL_RPATH ${PROJECT_INSTALL_RPATH})
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH YES)

# 自动生成的工具脚本
configure_file("${CMAKE_CURRENT_LIST_DIR}/project/tools/prepare-dependency-dll.sh.in"
               "${PROJECT_GENERATED_TOOLS_DIR}/prepare-dependency-dll.sh" ESCAPE_QUOTES @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/project/tools/prepare-archive.sh.in"
               "${PROJECT_GENERATED_TOOLS_DIR}/prepare-archive.sh" ESCAPE_QUOTES @ONLY)
add_custom_target(
  "tools-prepare-dependency" ALL
  COMMAND "${CMAKE_COMMAND}" "-E" "copy_if_different" "${PROJECT_GENERATED_TOOLS_DIR}/prepare-dependency-dll.sh"
          "${PROJECT_INSTALL_TOOLS_DIR}/script/"
  BYPRODUCTS "${PROJECT_INSTALL_TOOLS_DIR}/script/prepare-dependency-dll.sh")
add_custom_target(
  "tools-prepare-archive" ALL
  COMMAND "${CMAKE_COMMAND}" "-E" "copy_if_different" "${PROJECT_GENERATED_TOOLS_DIR}/prepare-archive.sh"
          "${PROJECT_INSTALL_TOOLS_DIR}/script/"
  BYPRODUCTS "${PROJECT_INSTALL_TOOLS_DIR}/script/prepare-archive.sh")

set_property(TARGET "tools-prepare-dependency" PROPERTY FOLDER "${PROJECT_NAME}/tools")
set_property(TARGET "tools-prepare-archive" PROPERTY FOLDER "${PROJECT_NAME}/tools")

if(ATFRAMEWORK_CMAKE_TOOLSET_TARGET_IS_LINUX)
  configure_file("${CMAKE_CURRENT_LIST_DIR}/project/tools/copy-dependency-dll.sh.in"
                 "${PROJECT_GENERATED_TOOLS_DIR}/copy-dependency-dll.sh" ESCAPE_QUOTES @ONLY)
  list(APPEND PROJECT_RUNTIME_POST_BUILD_DYNAMIC_LIBRARY_BASH "${PROJECT_GENERATED_TOOLS_DIR}/copy-dependency-dll.sh")
  list(APPEND PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_LIBRARY_BASH
       "${PROJECT_GENERATED_TOOLS_DIR}/copy-dependency-dll.sh")
endif()

# 导入所有工程项目
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/atframework")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/src")

if(PROJECT_ENABLE_SAMPLE)
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/sample")
endif()

# Copy 资源和执行清理流程
file(
  GLOB PROJECT_INSTALL_DST_LIST
  LIST_DIRECTORIES TRUE
  "${CMAKE_CURRENT_LIST_DIR}/install/*")
project_link_or_copy_files(${PROJECT_INSTALL_DST_LIST} "${PROJECT_INSTALL_BAS_DIR}")

configure_file("${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.sh.in" "${PROJECT_GENERATED_TOOLS_DIR}/cleanup-dll.sh"
               ESCAPE_QUOTES @ONLY)

configure_file("${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.ps1.in" "${PROJECT_GENERATED_TOOLS_DIR}/cleanup-dll.ps1"
               ESCAPE_QUOTES @ONLY)

add_custom_target(
  "tools-cleanup-dll" ALL
  COMMAND "${CMAKE_COMMAND}" "-E" "copy_if_different" "${PROJECT_GENERATED_TOOLS_DIR}/cleanup-dll.sh"
          "${PROJECT_GENERATED_TOOLS_DIR}/cleanup-dll.ps1" "${PROJECT_INSTALL_BAS_DIR}"
  BYPRODUCTS "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.sh" "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.ps1")

add_custom_target("run-cleanup-dll" ALL DEPENDS "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.sh"
                                                "${PROJECT_INSTALL_BAS_DIR}/cleanup-dll.ps1")

set_property(TARGET "tools-cleanup-dll" PROPERTY FOLDER "${PROJECT_NAME}/tools")
set_property(TARGET "run-cleanup-dll" PROPERTY FOLDER "${PROJECT_NAME}/tools")

if(CYGWIN
   OR UNIX
   OR APPLE
   OR NOT ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
  add_custom_command(
    TARGET "run-cleanup-dll"
    POST_BUILD
    COMMAND "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${PROJECT_GENERATED_TOOLS_DIR}/cleanup-dll.sh" "-i" "-d"
            "${PROJECT_INSTALL_BAS_DIR}"
    WORKING_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}"
    COMMENT "Cleanup old dlls...")
else()
  add_custom_command(
    TARGET "run-cleanup-dll"
    POST_BUILD
    COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" "-NoProfile" "-InputFormat" "None" "-ExecutionPolicy" "Bypass"
      "-NonInteractive" "-NoLogo" "-File" "${PROJECT_GENERATED_TOOLS_DIR}/cleanup-dll.ps1"
      "-IgnoreRunningDynamicLibraryChecking" "-DeployBaseDirectory" "${PROJECT_INSTALL_BAS_DIR}"
    WORKING_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}"
    COMMENT "Cleanup old dlls...")
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
