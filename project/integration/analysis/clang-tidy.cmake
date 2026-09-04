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

if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  message(
    WARNING
      "The ${CMAKE_GENERATOR} generator does not provide compile_commands.json. "
      "The clang-tidy target is disabled; configure with Ninja or a Makefile generator for complete code analysis.")
  return()
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Minimum Clang frontend version required by the compiler environment.
#
# The MSVC STL intentionally rejects Clang frontends older than the version recorded in yvals_core.h. Do not bypass
# that guard: removed STL workarounds can make an older clang-tidy parse the translation unit incorrectly.
# ---------------------------------------------------------------------------------------------------------------------
set(project_code_analysis_clang_tidy_msvc_stl_minimum_version 0)
set(project_code_analysis_clang_tidy_msvc_stl_header)
if(CMAKE_HOST_WIN32 AND (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
  set(project_code_analysis_clang_tidy_msvc_stl_header_candidates)
  if(DEFINED ENV{VCToolsInstallDir})
    list(APPEND project_code_analysis_clang_tidy_msvc_stl_header_candidates
         "$ENV{VCToolsInstallDir}/include/yvals_core.h")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    get_filename_component(project_code_analysis_clang_tidy_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(project_code_analysis_clang_tidy_msvc_toolset_directory
                           "${project_code_analysis_clang_tidy_compiler_directory}/../../.." ABSOLUTE)
    list(APPEND project_code_analysis_clang_tidy_msvc_stl_header_candidates
         "${project_code_analysis_clang_tidy_msvc_toolset_directory}/include/yvals_core.h")
  endif()

  foreach(project_code_analysis_clang_tidy_msvc_stl_header_candidate IN LISTS
                                                                        project_code_analysis_clang_tidy_msvc_stl_header_candidates)
    if(EXISTS "${project_code_analysis_clang_tidy_msvc_stl_header_candidate}")
      set(project_code_analysis_clang_tidy_msvc_stl_header
          "${project_code_analysis_clang_tidy_msvc_stl_header_candidate}")
      break()
    endif()
  endforeach()
endif()

if(project_code_analysis_clang_tidy_msvc_stl_header)
  file(READ "${project_code_analysis_clang_tidy_msvc_stl_header}"
       project_code_analysis_clang_tidy_msvc_stl_header_content)
  string(
    REGEX MATCH "expected Clang[ \t]+([0-9]+)(\\.[0-9]+)*[ \t]+or newer"
                project_code_analysis_clang_tidy_msvc_stl_version_match
                "${project_code_analysis_clang_tidy_msvc_stl_header_content}")
  if(project_code_analysis_clang_tidy_msvc_stl_version_match)
    set(project_code_analysis_clang_tidy_msvc_stl_minimum_version "${CMAKE_MATCH_1}")
  endif()
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Probe one clang-tidy candidate. A candidate is usable only when it runs, exposes a parsable version, satisfies the
# compiler environment minimum and parses every top-level option in the repository .clang-tidy. --dump-config loads
# the configuration exactly like an analysis run; unknown top-level keys (for example ExcludeHeaderFilterRegex on
# clang-tidy 18 and older) are reported as "unknown key" / "Error parsing" without a failing exit code, so the
# diagnostics themselves have to be matched.
# ---------------------------------------------------------------------------------------------------------------------
function(project_code_analysis_clang_tidy_probe_candidate candidate out_supported out_version out_major out_reason)
  set(${out_supported} FALSE PARENT_SCOPE)
  set(${out_version} "" PARENT_SCOPE)
  set(${out_major} "" PARENT_SCOPE)
  set(${out_reason} "" PARENT_SCOPE)

  execute_process(
    COMMAND "${candidate}" --version
    RESULT_VARIABLE project_code_analysis_clang_tidy_probe_result
    OUTPUT_VARIABLE project_code_analysis_clang_tidy_probe_output
    ERROR_VARIABLE project_code_analysis_clang_tidy_probe_error
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE
    TIMEOUT 10)
  if(NOT project_code_analysis_clang_tidy_probe_result STREQUAL "0")
    set(${out_reason} "could not be executed" PARENT_SCOPE)
    return()
  endif()

  string(
    REGEX MATCH "[Vv]ersion[ \t]+([0-9]+(\\.[0-9]+)+)"
                project_code_analysis_clang_tidy_probe_version_match
                "${project_code_analysis_clang_tidy_probe_output}\n${project_code_analysis_clang_tidy_probe_error}")
  if(NOT project_code_analysis_clang_tidy_probe_version_match)
    set(${out_reason} "version could not be determined" PARENT_SCOPE)
    return()
  endif()
  set(project_code_analysis_clang_tidy_probe_version "${CMAKE_MATCH_1}")
  string(REGEX MATCH "^[0-9]+" project_code_analysis_clang_tidy_probe_major
               "${project_code_analysis_clang_tidy_probe_version}")
  set(${out_version} "${project_code_analysis_clang_tidy_probe_version}" PARENT_SCOPE)
  set(${out_major} "${project_code_analysis_clang_tidy_probe_major}" PARENT_SCOPE)

  if(project_code_analysis_clang_tidy_probe_major LESS project_code_analysis_clang_tidy_msvc_stl_minimum_version)
    set(${out_reason}
        "is incompatible with the installed MSVC STL, which requires Clang ${project_code_analysis_clang_tidy_msvc_stl_minimum_version} or newer"
        PARENT_SCOPE)
    return()
  endif()

  if(EXISTS "${PROJECT_SOURCE_DIR}/.clang-tidy")
    execute_process(
      COMMAND "${candidate}" --dump-config
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      RESULT_VARIABLE project_code_analysis_clang_tidy_probe_dump_result
      OUTPUT_VARIABLE project_code_analysis_clang_tidy_probe_dump_output
      ERROR_VARIABLE project_code_analysis_clang_tidy_probe_dump_error
      TIMEOUT 30)
    set(project_code_analysis_clang_tidy_probe_dump_text
        "${project_code_analysis_clang_tidy_probe_dump_output}\n${project_code_analysis_clang_tidy_probe_dump_error}")
    if(NOT project_code_analysis_clang_tidy_probe_dump_result STREQUAL "0"
       OR project_code_analysis_clang_tidy_probe_dump_text MATCHES
          "[Uu]nknown[ \t]+key|[Ee]rror[ \t]+parsing")
      set(${out_reason} "does not support every option in ${PROJECT_SOURCE_DIR}/.clang-tidy" PARENT_SCOPE)
      return()
    endif()
  endif()

  set(${out_supported} TRUE PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------------------------------------------------
# Select the newest usable candidate. Also report the newest rejected candidate so the failure message can explain why
# the best available clang-tidy was not accepted.
# ---------------------------------------------------------------------------------------------------------------------
function(project_code_analysis_clang_tidy_select_candidate out_executable out_version out_major out_rejection)
  set(best_executable "")
  set(best_version "")
  set(best_major "")
  set(newest_rejected "")
  set(newest_rejected_version "")
  set(newest_rejected_reason "")
  foreach(candidate IN LISTS ARGN)
    project_code_analysis_clang_tidy_probe_candidate("${candidate}" probe_supported probe_version probe_major
                                                     probe_reason)
    if(probe_supported)
      if(best_executable STREQUAL "" OR probe_version VERSION_GREATER best_version)
        set(best_executable "${candidate}")
        set(best_version "${probe_version}")
        set(best_major "${probe_major}")
      endif()
    else()
      if(probe_version STREQUAL "")
        set(probe_version "0")
      endif()
      if(newest_rejected STREQUAL "" OR probe_version VERSION_GREATER newest_rejected_version)
        set(newest_rejected "${candidate}")
        set(newest_rejected_version "${probe_version}")
        set(newest_rejected_reason "${probe_reason}")
      endif()
    endif()
  endforeach()

  set(${out_executable} "${best_executable}" PARENT_SCOPE)
  set(${out_version} "${best_version}" PARENT_SCOPE)
  set(${out_major} "${best_major}" PARENT_SCOPE)
  if(newest_rejected STREQUAL "")
    set(${out_rejection} "" PARENT_SCOPE)
  else()
    if(newest_rejected_version STREQUAL "0")
      set(newest_rejected_version "unknown version")
    endif()
    set(${out_rejection} "${newest_rejected} (${newest_rejected_version}) ${newest_rejected_reason}" PARENT_SCOPE)
  endif()
endfunction()

# ---------------------------------------------------------------------------------------------------------------------
# Discover clang-tidy. An explicit PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE wins. Otherwise pick the newest usable
# version on PATH first, then the official package default installation directories (which may not be on PATH).
# ---------------------------------------------------------------------------------------------------------------------
set(project_code_analysis_clang_tidy_candidates)
set(project_code_analysis_clang_tidy_explicit_override FALSE)
if(PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE)
  set(project_code_analysis_clang_tidy_explicit_override TRUE)
  list(APPEND project_code_analysis_clang_tidy_candidates "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}")
else()
  set(project_code_analysis_clang_tidy_path_entries "$ENV{PATH}")
  if(NOT CMAKE_HOST_WIN32)
    string(REPLACE ":" ";" project_code_analysis_clang_tidy_path_entries
                   "${project_code_analysis_clang_tidy_path_entries}")
  endif()
  foreach(project_code_analysis_clang_tidy_path_entry IN LISTS project_code_analysis_clang_tidy_path_entries)
    if(project_code_analysis_clang_tidy_path_entry STREQUAL "")
      continue()
    endif()
    file(
      GLOB project_code_analysis_clang_tidy_path_matches
      LIST_DIRECTORIES false
      "${project_code_analysis_clang_tidy_path_entry}/clang-tidy"
      "${project_code_analysis_clang_tidy_path_entry}/clang-tidy.exe"
      "${project_code_analysis_clang_tidy_path_entry}/clang-tidy-*")
    list(APPEND project_code_analysis_clang_tidy_candidates ${project_code_analysis_clang_tidy_path_matches})
  endforeach()
  if(project_code_analysis_clang_tidy_candidates)
    list(REMOVE_DUPLICATES project_code_analysis_clang_tidy_candidates)
  endif()
endif()

project_code_analysis_clang_tidy_select_candidate(
  project_code_analysis_clang_tidy_executable project_code_analysis_clang_tidy_version
  project_code_analysis_clang_tidy_major project_code_analysis_clang_tidy_rejection
  ${project_code_analysis_clang_tidy_candidates})

if(project_code_analysis_clang_tidy_executable STREQUAL "" AND NOT project_code_analysis_clang_tidy_explicit_override)
  set(project_code_analysis_clang_tidy_fallback_candidates)
  set(project_code_analysis_clang_tidy_fallback_directories)
  if(DEFINED ENV{LLVM_ROOT})
    list(APPEND project_code_analysis_clang_tidy_fallback_directories "$ENV{LLVM_ROOT}/bin")
  endif()
  if(DEFINED ENV{LLVM_HOME})
    list(APPEND project_code_analysis_clang_tidy_fallback_directories "$ENV{LLVM_HOME}/bin")
  endif()
  if(CMAKE_HOST_WIN32 AND DEFINED ENV{ProgramFiles})
    list(APPEND project_code_analysis_clang_tidy_fallback_directories "$ENV{ProgramFiles}/LLVM/bin")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND project_code_analysis_clang_tidy_fallback_directories "/opt/homebrew/opt/llvm/bin"
         "/usr/local/opt/llvm/bin")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    file(
      GLOB project_code_analysis_clang_tidy_llvm_package_directories
      LIST_DIRECTORIES true
      "/usr/lib/llvm-*/bin")
    list(APPEND project_code_analysis_clang_tidy_fallback_directories
         ${project_code_analysis_clang_tidy_llvm_package_directories})
  endif()
  foreach(project_code_analysis_clang_tidy_fallback_directory IN LISTS
                                                                  project_code_analysis_clang_tidy_fallback_directories)
    file(
      GLOB project_code_analysis_clang_tidy_fallback_matches
      LIST_DIRECTORIES false
      "${project_code_analysis_clang_tidy_fallback_directory}/clang-tidy"
      "${project_code_analysis_clang_tidy_fallback_directory}/clang-tidy.exe"
      "${project_code_analysis_clang_tidy_fallback_directory}/clang-tidy-*")
    list(APPEND project_code_analysis_clang_tidy_fallback_candidates
         ${project_code_analysis_clang_tidy_fallback_matches})
  endforeach()
  if(project_code_analysis_clang_tidy_fallback_candidates)
    if(project_code_analysis_clang_tidy_candidates)
      list(REMOVE_ITEM project_code_analysis_clang_tidy_fallback_candidates
           ${project_code_analysis_clang_tidy_candidates})
    endif()
    if(project_code_analysis_clang_tidy_fallback_candidates)
      list(REMOVE_DUPLICATES project_code_analysis_clang_tidy_fallback_candidates)
      project_code_analysis_clang_tidy_select_candidate(
        project_code_analysis_clang_tidy_executable project_code_analysis_clang_tidy_version
        project_code_analysis_clang_tidy_major project_code_analysis_clang_tidy_fallback_rejection
        ${project_code_analysis_clang_tidy_fallback_candidates})
      if(NOT project_code_analysis_clang_tidy_fallback_rejection STREQUAL "")
        set(project_code_analysis_clang_tidy_rejection "${project_code_analysis_clang_tidy_fallback_rejection}")
      endif()
    endif()
  endif()
endif()

if(project_code_analysis_clang_tidy_executable STREQUAL "")
  if(project_code_analysis_clang_tidy_explicit_override)
    set(project_code_analysis_clang_tidy_failure_reason
        "The clang-tidy executable set via PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE is unusable.")
    string(APPEND project_code_analysis_clang_tidy_failure_reason " ${project_code_analysis_clang_tidy_rejection}.")
  else()
    set(project_code_analysis_clang_tidy_failure_reason
        "No clang-tidy that supports every option in the repository .clang-tidy and satisfies the detected compiler")
    string(APPEND project_code_analysis_clang_tidy_failure_reason
           " environment was found on PATH or in the default LLVM installation directories.")
    if(NOT project_code_analysis_clang_tidy_rejection STREQUAL "")
      string(APPEND project_code_analysis_clang_tidy_failure_reason
             " Newest rejected candidate: ${project_code_analysis_clang_tidy_rejection}.")
    endif()
  endif()
  project_code_analysis_clang_tidy_print_install_hint("${project_code_analysis_clang_tidy_failure_reason}")
  return()
endif()

if(NOT project_code_analysis_clang_tidy_explicit_override)
  set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE
      "${project_code_analysis_clang_tidy_executable}"
      CACHE FILEPATH "Path to the clang-tidy executable used for incremental code analysis.")
endif()
mark_as_advanced(PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE)
set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAJOR_VERSION "${project_code_analysis_clang_tidy_major}")

set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES
    5
    CACHE STRING "Maximum number of clang-tidy issues allowed in staged, unstaged, and unpushed files.")
if(NOT PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES MATCHES "^[0-9]+$")
  message(FATAL_ERROR "PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES must be a non-negative integer, "
                      "got: ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES}")
endif()

set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_JOBS
    8
    CACHE STRING "Number of files analyzed by clang-tidy in parallel.")
if(NOT PROJECT_CODE_ANALYSIS_CLANG_TIDY_JOBS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "PROJECT_CODE_ANALYSIS_CLANG_TIDY_JOBS must be a positive integer, "
                      "got: ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_JOBS}")
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
  if(NOT Python3_EXECUTABLE OR NOT EXISTS "${Python3_EXECUTABLE}")
    message(
      WARNING
        "clang-tidy needs the configured Python interpreter to prepare an analysis-only compilation database without "
        "CMake's MSVC precompiled-header arguments. The clang-tidy target is disabled.")
    return()
  endif()
  if(CMAKE_HOST_WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
    list(APPEND _project_code_analysis_clang_tidy_msvc_arguments "-PrepareMsvcCompilationDatabase")
  else()
    list(APPEND _project_code_analysis_clang_tidy_msvc_arguments "--prepare-msvc-compilation-database")
  endif()
endif()

if(CMAKE_HOST_WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
  set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" "-NoLogo" "-NoProfile" "-NonInteractive" "-ExecutionPolicy" "Bypass" "-File"
      "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.ps1" "-RepositoryRoot" "${PROJECT_SOURCE_DIR}" "-BuildDirectory"
      "${CMAKE_BINARY_DIR}" "-ClangTidyExecutable" "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}" "-MaxIssues"
      "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES}" "-ClangTidyMajorVersion"
      "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAJOR_VERSION}" "-PythonExecutable" "${Python3_EXECUTABLE}" "-PrepareScript"
      "${CMAKE_CURRENT_LIST_DIR}/clang-tidy-prepare.py" "-ReportDirectory" "${CMAKE_BINARY_DIR}/_agent_tmp/clang-tidy"
      "-Jobs" "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_JOBS}" ${_project_code_analysis_clang_tidy_msvc_arguments})
elseif(ATFRAMEWORK_CMAKE_TOOLSET_BASH)
  set(PROJECT_CODE_ANALYSIS_CLANG_TIDY_COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.sh" "--repository-root"
      "${PROJECT_SOURCE_DIR}" "--build-directory" "${CMAKE_BINARY_DIR}" "--clang-tidy-executable"
      "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}" "--max-issues" "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAX_ISSUES}"
      "--clang-tidy-major-version" "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_MAJOR_VERSION}" "--python-executable"
      "${Python3_EXECUTABLE}" "--prepare-script" "${CMAKE_CURRENT_LIST_DIR}/clang-tidy-prepare.py" "--report-directory"
      "${CMAKE_BINARY_DIR}/_agent_tmp/clang-tidy" "--jobs" "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_JOBS}"
      ${_project_code_analysis_clang_tidy_msvc_arguments})
else()
  message(
    WARNING
      "clang-tidy was found, but neither PowerShell 7+ nor a Bash runner is available; clang-tidy analysis is disabled."
  )
  return()
endif()

message(
  STATUS
    "Code analysis: using clang-tidy ${project_code_analysis_clang_tidy_version}: ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_EXECUTABLE}"
)
add_custom_target(
  "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_TARGET}" ALL
  COMMAND ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_COMMAND}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMENT "Run clang-tidy on staged, unstaged, and unpushed C/C++ files"
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.ps1" "${CMAKE_CURRENT_LIST_DIR}/clang-tidy.sh"
          "${CMAKE_CURRENT_LIST_DIR}/clang-tidy-prepare.py"
  USES_TERMINAL VERBATIM)
set_property(TARGET "${PROJECT_CODE_ANALYSIS_CLANG_TIDY_TARGET}" PROPERTY FOLDER "${PROJECT_NAME}/tools/analysis")

if(PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES)
  add_dependencies("${PROJECT_CODE_ANALYSIS_CLANG_TIDY_TARGET}" ${PROJECT_CODE_ANALYSIS_CLANG_TIDY_DEPENDENCIES})
endif()
