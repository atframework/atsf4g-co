# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =====================================================================================================================
# Optimize the workspace .vscode/settings.json for atsf4g-co.
#
# This script discovers the compiler, build directory and helper tools, then delegates the JSON edits to the sibling
# ``optimize_vscode_settings.py``. It is idempotent and preserves user-provided values whenever possible.
#
# Run in script mode, e.g.:
#   cmake -DPROJECT_VSCODE_BUILD_DIR=<build dir> -P project/integration/vscode/OptimizeVsCodeSettings.cmake
#
# It may also be ``include()``-d from a configured project, in which case the live ``CMAKE_CXX_COMPILER*`` and
# ``CMAKE_BINARY_DIR`` values are used automatically.
#
# Optional inputs (all overridable via -D):
#   PROJECT_VSCODE_WORKSPACE_DIR - repository root that holds .vscode/ (default: three levels above this script)
#   PROJECT_VSCODE_BUILD_DIR     - build tree with CMakeCache.txt (default: live CMAKE_BINARY_DIR or a detected tree)
#   PROJECT_VSCODE_PYTHON        - Python interpreter to use (default: discovered via find_program)
#   PROJECT_VSCODE_DRY_RUN       - when truthy, report changes without writing
# =====================================================================================================================

# ---------------------------------------------------------------------------------------------------------------------
# Resolve the workspace directory (the folder that owns .vscode/).
# ---------------------------------------------------------------------------------------------------------------------
if(NOT DEFINED PROJECT_VSCODE_WORKSPACE_DIR OR PROJECT_VSCODE_WORKSPACE_DIR STREQUAL "")
  get_filename_component(PROJECT_VSCODE_WORKSPACE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Resolve the build directory. Prefer an explicit value, then the live binary dir, then a couple of common defaults.
# When ``include()``-d from the top-level project ``CMAKE_BINARY_DIR`` equals the current build tree
# (``CMAKE_CURRENT_BINARY_DIR`` at the project root), which is the directory whose change events dominate editor
# latency and that the optimizer excludes from the VSCode watcher/search via ``--build-dir``.
# ---------------------------------------------------------------------------------------------------------------------
if(NOT DEFINED PROJECT_VSCODE_BUILD_DIR OR PROJECT_VSCODE_BUILD_DIR STREQUAL "")
  if(DEFINED CMAKE_BINARY_DIR
     AND NOT CMAKE_BINARY_DIR STREQUAL ""
     AND EXISTS "${CMAKE_BINARY_DIR}/CMakeCache.txt")
    set(PROJECT_VSCODE_BUILD_DIR "${CMAKE_BINARY_DIR}")
  else()
    foreach(PROJECT_VSCODE_BUILD_CANDIDATE "build_jobs_cmake_tools" "build")
      if(EXISTS "${PROJECT_VSCODE_WORKSPACE_DIR}/${PROJECT_VSCODE_BUILD_CANDIDATE}/CMakeCache.txt")
        set(PROJECT_VSCODE_BUILD_DIR "${PROJECT_VSCODE_WORKSPACE_DIR}/${PROJECT_VSCODE_BUILD_CANDIDATE}")
        break()
      endif()
    endforeach()
  endif()
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Resolve compiler identity and path. Prefer live configure-time variables, otherwise read the build tree.
# ---------------------------------------------------------------------------------------------------------------------
set(PROJECT_VSCODE_COMPILER_ID "")
set(PROJECT_VSCODE_COMPILER_PATH "")
if(DEFINED CMAKE_CXX_COMPILER_ID AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "")
  set(PROJECT_VSCODE_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
endif()
if(DEFINED CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL "")
  set(PROJECT_VSCODE_COMPILER_PATH "${CMAKE_CXX_COMPILER}")
endif()

if(PROJECT_VSCODE_BUILD_DIR AND EXISTS "${PROJECT_VSCODE_BUILD_DIR}/CMakeCache.txt")
  if(PROJECT_VSCODE_COMPILER_PATH STREQUAL "")
    file(STRINGS "${PROJECT_VSCODE_BUILD_DIR}/CMakeCache.txt" PROJECT_VSCODE_CACHE_CXX
         REGEX "^CMAKE_CXX_COMPILER:[^=]*=")
    foreach(PROJECT_VSCODE_CACHE_LINE IN LISTS PROJECT_VSCODE_CACHE_CXX)
      if(PROJECT_VSCODE_CACHE_LINE MATCHES "^CMAKE_CXX_COMPILER:[^=]*=(.+)$")
        set(PROJECT_VSCODE_COMPILER_PATH "${CMAKE_MATCH_1}")
      endif()
    endforeach()
  endif()
  if(PROJECT_VSCODE_COMPILER_ID STREQUAL "")
    file(GLOB PROJECT_VSCODE_COMPILER_CMAKE "${PROJECT_VSCODE_BUILD_DIR}/CMakeFiles/*/CMakeCXXCompiler.cmake")
    foreach(PROJECT_VSCODE_COMPILER_FILE IN LISTS PROJECT_VSCODE_COMPILER_CMAKE)
      file(STRINGS "${PROJECT_VSCODE_COMPILER_FILE}" PROJECT_VSCODE_COMPILER_ID_LINES REGEX "CMAKE_CXX_COMPILER_ID")
      foreach(PROJECT_VSCODE_COMPILER_ID_LINE IN LISTS PROJECT_VSCODE_COMPILER_ID_LINES)
        if(PROJECT_VSCODE_COMPILER_ID_LINE MATCHES "set\\(CMAKE_CXX_COMPILER_ID \"([^\"]+)\"\\)")
          set(PROJECT_VSCODE_COMPILER_ID "${CMAKE_MATCH_1}")
        endif()
      endforeach()
    endforeach()
  endif()
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Discover the Python interpreter and helper tools.
# ---------------------------------------------------------------------------------------------------------------------
if(NOT Python3_EXECUTABLE)
  find_package(Python3 COMPONENTS Interpreter)
endif()
if(NOT Python3_EXECUTABLE)
  if(PROJECT_VSCODE_ALLOW_SKIP)
    message(WARNING "[optimize-vscode] No Python interpreter found; skipping .vscode/settings.json optimization.")
    return()
  endif()
  message(FATAL_ERROR "[optimize-vscode] No Python interpreter found (looked for python3/python/py).")
endif()

find_program(PROJECT_VSCODE_CPPLINT NAMES cpplint)
find_program(PROJECT_VSCODE_CLANG_FORMAT NAMES clang-format)

# ---------------------------------------------------------------------------------------------------------------------
# Assemble the command line and run the optimizer.
# ---------------------------------------------------------------------------------------------------------------------
set(PROJECT_VSCODE_PY_ARGS
    "${CMAKE_CURRENT_LIST_DIR}/optimize_vscode_settings.py" "--settings"
    "${PROJECT_VSCODE_WORKSPACE_DIR}/.vscode/settings.json" "--workspace-dir" "${PROJECT_VSCODE_WORKSPACE_DIR}"
    "--quiet")
if(PROJECT_VSCODE_BUILD_DIR)
  list(APPEND PROJECT_VSCODE_PY_ARGS "--build-dir" "${PROJECT_VSCODE_BUILD_DIR}")
endif()
if(NOT PROJECT_VSCODE_COMPILER_ID STREQUAL "")
  list(APPEND PROJECT_VSCODE_PY_ARGS "--compiler-id" "${PROJECT_VSCODE_COMPILER_ID}")
endif()
if(NOT PROJECT_VSCODE_COMPILER_PATH STREQUAL "")
  list(APPEND PROJECT_VSCODE_PY_ARGS "--compiler-path" "${PROJECT_VSCODE_COMPILER_PATH}")
endif()
if(PROJECT_VSCODE_CPPLINT)
  list(APPEND PROJECT_VSCODE_PY_ARGS "--cpplint" "${PROJECT_VSCODE_CPPLINT}")
endif()
if(PROJECT_VSCODE_CLANG_FORMAT)
  list(APPEND PROJECT_VSCODE_PY_ARGS "--clang-format" "${PROJECT_VSCODE_CLANG_FORMAT}")
endif()
if(PROJECT_VSCODE_DRY_RUN)
  list(APPEND PROJECT_VSCODE_PY_ARGS "--dry-run")
endif()

message(
  STATUS
    "[VSCode Integration]
    Workspace       : ${PROJECT_VSCODE_WORKSPACE_DIR}
    Build directory : ${PROJECT_VSCODE_BUILD_DIR}
    Compiler        : ${PROJECT_VSCODE_COMPILER_ID} (${PROJECT_VSCODE_COMPILER_PATH})")

execute_process(COMMAND "${Python3_EXECUTABLE}" ${PROJECT_VSCODE_PY_ARGS} RESULT_VARIABLE PROJECT_VSCODE_RESULT)
if(NOT PROJECT_VSCODE_RESULT EQUAL 0)
  if(PROJECT_VSCODE_ALLOW_SKIP)
    message(WARNING "[optimize-vscode] optimizer exited with code ${PROJECT_VSCODE_RESULT}; continuing configure.")
  else()
    message(FATAL_ERROR "[optimize-vscode] optimizer failed with exit code ${PROJECT_VSCODE_RESULT}.")
  endif()
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Clean stale clangd preamble cache (preamble-*.pch) from the system temp directory.
# clangd serializes each file's preamble to the system temp dir (default --pch-storage=disk) and the files persist
# across editor restarts. They accumulate (one project can produce hundreds of 40-90MB files = tens of GB) and become
# stale whenever the build tree changes. Removing them once per configure keeps the temp dir bounded; clangd simply
# regenerates the preamble for any file it opens again. Best-effort, never fatal.
#
# Temp dir resolution mirrors LLVM's sys::path (what clangd uses) per platform:
#   Windows:  $TEMP / $TMP            -> default %LOCALAPPDATA%\Temp
#   Linux:    $TMPDIR                 -> falls back to /tmp when unset
#   macOS:    $TMPDIR                 -> set by launchd to /var/folders/... (rarely unset)
# ---------------------------------------------------------------------------------------------------------------------
set(PROJECT_VSCODE_CLANGD_TMP "")
if(WIN32)
  foreach(PROJECT_VSCODE_TMP_CANDIDATE "$ENV{TEMP}" "$ENV{TMP}")
    if(NOT PROJECT_VSCODE_CLANGD_TMP
       AND NOT "${PROJECT_VSCODE_TMP_CANDIDATE}" STREQUAL ""
       AND IS_DIRECTORY "${PROJECT_VSCODE_TMP_CANDIDATE}")
      set(PROJECT_VSCODE_CLANGD_TMP "${PROJECT_VSCODE_TMP_CANDIDATE}")
    endif()
  endforeach()
else()
  if(NOT "$ENV{TMPDIR}" STREQUAL "" AND IS_DIRECTORY "$ENV{TMPDIR}")
    set(PROJECT_VSCODE_CLANGD_TMP "$ENV{TMPDIR}")
  elseif(IS_DIRECTORY "/tmp")
    set(PROJECT_VSCODE_CLANGD_TMP "/tmp")
  endif()
endif()
if(PROJECT_VSCODE_CLANGD_TMP)
  file(GLOB PROJECT_VSCODE_STALE_PREAMBLES "${PROJECT_VSCODE_CLANGD_TMP}/preamble-*.pch")
  list(LENGTH PROJECT_VSCODE_STALE_PREAMBLES PROJECT_VSCODE_STALE_COUNT)
  if(PROJECT_VSCODE_STALE_COUNT GREATER 0)
    set(PROJECT_VSCODE_STALE_BYTES 0)
    foreach(PROJECT_VSCODE_PCH IN LISTS PROJECT_VSCODE_STALE_PREAMBLES)
      file(SIZE "${PROJECT_VSCODE_PCH}" PROJECT_VSCODE_PCH_SIZE)
      math(EXPR PROJECT_VSCODE_STALE_BYTES "${PROJECT_VSCODE_STALE_BYTES} + ${PROJECT_VSCODE_PCH_SIZE}")
    endforeach()
    math(EXPR PROJECT_VSCODE_STALE_MB "${PROJECT_VSCODE_STALE_BYTES} / 1048576")
    foreach(PROJECT_VSCODE_PCH IN LISTS PROJECT_VSCODE_STALE_PREAMBLES)
      file(REMOVE "${PROJECT_VSCODE_PCH}")
    endforeach()
    message(STATUS "[optimize-vscode] Cleaned ${PROJECT_VSCODE_STALE_COUNT} stale clangd preamble files "
                   "(${PROJECT_VSCODE_STALE_MB} MB) from ${PROJECT_VSCODE_CLANGD_TMP}")
  endif()
endif()
