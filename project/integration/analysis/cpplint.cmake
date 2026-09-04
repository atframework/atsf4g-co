# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(PROJECT_CODE_ANALYSIS_CPPLINT_MAX_ISSUES
    5
    CACHE STRING "Maximum number of cpplint issues allowed in staged, unstaged, and unpushed files.")

if(NOT PROJECT_CODE_ANALYSIS_CPPLINT_MAX_ISSUES MATCHES "^[0-9]+$")
  message(FATAL_ERROR "PROJECT_CODE_ANALYSIS_CPPLINT_MAX_ISSUES must be a non-negative integer, "
                      "got: ${PROJECT_CODE_ANALYSIS_CPPLINT_MAX_ISSUES}")
endif()

set(PROJECT_CODE_ANALYSIS_CPPLINT_TARGET "${PROJECT_NAME}-cpplint")
if(CMAKE_HOST_WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
  set(PROJECT_CODE_ANALYSIS_CPPLINT_COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" "-NoLogo" "-NoProfile" "-NonInteractive" "-ExecutionPolicy" "Bypass" "-File"
      "${CMAKE_CURRENT_LIST_DIR}/cpplint.ps1" "-RepositoryRoot" "${PROJECT_SOURCE_DIR}" "-PythonVenvDir"
      "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}" "-MaxIssues" "${PROJECT_CODE_ANALYSIS_CPPLINT_MAX_ISSUES}")
elseif(ATFRAMEWORK_CMAKE_TOOLSET_BASH)
  set(PROJECT_CODE_ANALYSIS_CPPLINT_COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${CMAKE_CURRENT_LIST_DIR}/cpplint.sh" "--repository-root"
      "${PROJECT_SOURCE_DIR}" "--python-venv-dir" "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}" "--max-issues"
      "${PROJECT_CODE_ANALYSIS_CPPLINT_MAX_ISSUES}" "--report-directory" "${CMAKE_BINARY_DIR}/_agent_tmp/cpplint")
else()
  message(
    WARNING "Code analysis is enabled, but neither PowerShell 7+ nor a Bash runner is available; cpplint is disabled.")
  return()
endif()

add_custom_target(
  "${PROJECT_CODE_ANALYSIS_CPPLINT_TARGET}" ALL
  COMMAND ${PROJECT_CODE_ANALYSIS_CPPLINT_COMMAND}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMENT "Run cpplint on staged, unstaged, and unpushed C/C++ files"
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/cpplint.ps1" "${CMAKE_CURRENT_LIST_DIR}/cpplint.sh"
  USES_TERMINAL VERBATIM)
set_property(TARGET "${PROJECT_CODE_ANALYSIS_CPPLINT_TARGET}" PROPERTY FOLDER "${PROJECT_NAME}/tools/analysis")

project_collect_compile_targets(PROJECT_CODE_ANALYSIS_CPPLINT_DEPENDENCIES "${PROJECT_SOURCE_DIR}")
if(PROJECT_CODE_ANALYSIS_CPPLINT_DEPENDENCIES)
  list(REMOVE_DUPLICATES PROJECT_CODE_ANALYSIS_CPPLINT_DEPENDENCIES)
  add_dependencies("${PROJECT_CODE_ANALYSIS_CPPLINT_TARGET}" ${PROJECT_CODE_ANALYSIS_CPPLINT_DEPENDENCIES})
endif()
