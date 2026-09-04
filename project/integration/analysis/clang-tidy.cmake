# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# Print the clang-tidy installation command for the current host platform.
function(project_code_analysis_clang_tidy_print_install_hint reason)
  set(project_code_analysis_clang_tidy_install_command)
  set(project_code_analysis_clang_tidy_install_note)
  if(CMAKE_HOST_WIN32)
    set(project_code_analysis_clang_tidy_install_command "winget install --id LLVM.LLVM --exact")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(project_code_analysis_clang_tidy_install_command "brew install llvm")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    cmake_host_system_information(RESULT project_code_analysis_clang_tidy_distribution_id QUERY DISTRIB_ID)
    cmake_host_system_information(RESULT project_code_analysis_clang_tidy_distribution_like QUERY DISTRIB_ID_LIKE)
    string(
      TOLOWER
        "${project_code_analysis_clang_tidy_distribution_id};${project_code_analysis_clang_tidy_distribution_like}"
        project_code_analysis_clang_tidy_distribution_family)
    if(project_code_analysis_clang_tidy_distribution_family MATCHES "debian|ubuntu")
      set(project_code_analysis_clang_tidy_install_command "sudo apt-get update && sudo apt-get install clang-tidy")
    elseif(project_code_analysis_clang_tidy_distribution_family MATCHES "rhel|redhat|fedora|centos|almalinux|rocky")
      set(project_code_analysis_clang_tidy_install_command "sudo dnf install clang-tools-extra")
      set(project_code_analysis_clang_tidy_install_note "  On older releases, replace dnf with yum.\n")
    elseif(project_code_analysis_clang_tidy_distribution_family MATCHES "arch|manjaro|endeavouros")
      set(project_code_analysis_clang_tidy_install_command "sudo pacman -S clang")
    else()
      set(project_code_analysis_clang_tidy_install_command
          "install the clang-tidy package with the system package manager")
    endif()
  else()
    set(project_code_analysis_clang_tidy_install_command
        "download LLVM from https://github.com/llvm/llvm-project/releases/latest")
  endif()

  message(
    STATUS "${reason} Install clang-tidy for the complete code analysis experience.\n"
           "  Suggested command: ${project_code_analysis_clang_tidy_install_command}\n"
           "${project_code_analysis_clang_tidy_install_note}"
           "  Then reconfigure, or set -DPROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE=<path>.")
endfunction()

set(_project_code_analysis_clang_tidy_names
    clang-tidy
    clang-tidy-23
    clang-tidy-22
    clang-tidy-21
    clang-tidy-20
    clang-tidy-19
    clang-tidy-18
    clang-tidy-17
    clang-tidy-16
    clang-tidy-15
    clang-tidy-14
    clang-tidy-13
    clang-tidy-12)
set(_project_code_analysis_clang_tidy_hints)
if(DEFINED ENV{LLVM_ROOT})
  list(APPEND _project_code_analysis_clang_tidy_hints "$ENV{LLVM_ROOT}/bin")
endif()
if(DEFINED ENV{LLVM_HOME})
  list(APPEND _project_code_analysis_clang_tidy_hints "$ENV{LLVM_HOME}/bin")
endif()
if(CMAKE_HOST_WIN32 AND DEFINED ENV{ProgramFiles})
  list(APPEND _project_code_analysis_clang_tidy_hints "$ENV{ProgramFiles}/LLVM/bin")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  list(APPEND _project_code_analysis_clang_tidy_hints "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin")
endif()

find_program(
  PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE
  NAMES ${_project_code_analysis_clang_tidy_names}
  HINTS ${_project_code_analysis_clang_tidy_hints}
  DOC "Path to the clang-tidy executable used for incremental code analysis.")
mark_as_advanced(PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE)

if(NOT PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE)
  project_code_analysis_clang_tidy_print_install_hint("clang-tidy was not found.")
  return()
endif()

execute_process(
  COMMAND "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}" --version
  RESULT_VARIABLE project_code_analysis_clang_tidy_version_result
  OUTPUT_QUIET ERROR_QUIET
  TIMEOUT 10)
if(NOT "${project_code_analysis_clang_tidy_version_result}" STREQUAL "0")
  project_code_analysis_clang_tidy_print_install_hint(
    "The detected clang-tidy executable could not be run: ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}.")
  return()
endif()

if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  message(
    WARNING
      "clang-tidy was found, but the ${CMAKE_GENERATOR} generator does not provide compile_commands.json. "
      "The clang-tidy target is disabled; configure with Ninja or a Makefile generator for complete code analysis.")
  return()
endif()

set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES
    10
    CACHE STRING "Maximum number of clang-tidy issues allowed in staged and unstaged files.")
if(NOT PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES MATCHES "^[0-9]+$")
  message(FATAL_ERROR "PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES must be a non-negative integer, "
                      "got: ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES}")
endif()

project_collect_compile_targets(PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES "${PROJECT_SOURCE_DIR}")
if(PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES)
  list(REMOVE_DUPLICATES PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES)
  foreach(project_code_analysis_clang_tidy_dependency IN LISTS PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES)
    set_property(TARGET "${project_code_analysis_clang_tidy_dependency}" PROPERTY EXPORT_COMPILE_COMMANDS ON)
  endforeach()
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_TARGET "${PROJECT_NAME}-clang-tidy")
set(_project_code_analysis_clang_tidy_msvc_arguments)
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  if(CMAKE_HOST_WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
    list(APPEND _project_code_analysis_clang_tidy_msvc_arguments "-DisableMsvcPrecompiledHeader")
  else()
    list(APPEND _project_code_analysis_clang_tidy_msvc_arguments "--disable-msvc-precompiled-header")
  endif()
endif()

if(CMAKE_HOST_WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
  set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" "-NoLogo" "-NoProfile" "-NonInteractive" "-ExecutionPolicy" "Bypass" "-File"
      "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.ps1" "-RepositoryRoot" "${PROJECT_SOURCE_DIR}" "-BuildDirectory"
      "${CMAKE_BINARY_DIR}" "-ClangTidyExecutable" "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}" "-MaxIssues"
      "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES}" ${_project_code_analysis_clang_tidy_msvc_arguments})
elseif(ATFRAMEWORK_CMAKE_TOOLSET_BASH)
  set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.sh" "--repository-root"
      "${PROJECT_SOURCE_DIR}" "--build-directory" "${CMAKE_BINARY_DIR}" "--clang-tidy-executable"
      "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}" "--max-issues" "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES}"
      "--report-directory" "${CMAKE_BINARY_DIR}/_agent_tmp/clang-tidy"
      ${_project_code_analysis_clang_tidy_msvc_arguments})
else()
  message(
    WARNING
      "clang-tidy was found, but neither PowerShell 7+ nor a Bash runner is available; clang-tidy analysis is disabled."
  )
  return()
endif()

message(STATUS "Code analysis: using clang-tidy: ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}")
add_custom_target(
  "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_TARGET}" ALL
  COMMAND ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_COMMAND}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMENT "Run clang-tidy on staged and unstaged C/C++ files"
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.ps1" "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.sh"
  USES_TERMINAL VERBATIM)
set_property(TARGET "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_TARGET}" PROPERTY FOLDER "${PROJECT_NAME}/tools/analysis")

if(PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES)
  add_dependencies("${PROJECT_CODE_ANALYSIS_CLANG_TIDY_TARGET}" ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES})
endif()
