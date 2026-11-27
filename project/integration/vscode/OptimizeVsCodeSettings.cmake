# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =====================================================================================================================
# Optimize the workspace .vscode/settings.json for atsf4g-co.
#
# This script discovers the compiler, build directory, third-party runtime directories, and helper tools, then delegates
# the JSON edits to the sibling ``optimize_vscode_settings.py``. It is idempotent and preserves user-provided values
# whenever possible.
#
# Normally ``include()``-d from a configured project (via project/integration/Bootstrap.cmake), using the live
# ``CMAKE_BINARY_DIR`` and configure-time variables directly.
#
# It may also be run in script mode from the build directory (where ``CMAKE_BINARY_DIR`` is the current directory): cd
# <build dir> && cmake -P <repo>/project/integration/vscode/OptimizeVsCodeSettings.cmake
# =====================================================================================================================

function(_project_optimize_vscode_settings)
  # Resolve the workspace directory (the repository root that owns .vscode/).
  if(DEFINED PROJECT_SOURCE_DIR AND NOT PROJECT_SOURCE_DIR STREQUAL "")
    get_filename_component(_workspace_dir "${PROJECT_SOURCE_DIR}" ABSOLUTE)
  else()
    get_filename_component(_workspace_dir "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
  endif()

  # Use the directory where cmake is executed (CMAKE_BINARY_DIR) as the build directory. In script mode (-P), skip
  # setting --build-dir if executed directly from the workspace root without CMakeCache.txt.
  set(_build_dir "")
  if(DEFINED CMAKE_BINARY_DIR AND NOT CMAKE_BINARY_DIR STREQUAL "")
    get_filename_component(_cmake_binary_dir_abs "${CMAKE_BINARY_DIR}" ABSOLUTE)
    if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
      set(_build_dir "${_cmake_binary_dir_abs}")
    elseif(NOT _cmake_binary_dir_abs STREQUAL _workspace_dir OR EXISTS "${_cmake_binary_dir_abs}/CMakeCache.txt")
      set(_build_dir "${_cmake_binary_dir_abs}")
    endif()
  endif()

  set(_cache_file "")
  if(NOT _build_dir STREQUAL "" AND EXISTS "${_build_dir}/CMakeCache.txt")
    set(_cache_file "${_build_dir}/CMakeCache.txt")
  endif()

  # Resolve compiler identity and path. Prefer live configure-time variables, otherwise read the build tree.
  set(_compiler_id "")
  set(_compiler_path "")
  if(DEFINED CMAKE_CXX_COMPILER_ID AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "")
    set(_compiler_id "${CMAKE_CXX_COMPILER_ID}")
  endif()
  if(DEFINED CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL "")
    set(_compiler_path "${CMAKE_CXX_COMPILER}")
  endif()
  if(NOT _cache_file STREQUAL "")
    if(_compiler_path STREQUAL "")
      file(STRINGS "${_cache_file}" _cache_cxx REGEX "^CMAKE_CXX_COMPILER:[^=]*=")
      foreach(_cache_line IN LISTS _cache_cxx)
        if(_cache_line MATCHES "^CMAKE_CXX_COMPILER:[^=]*=(.+)$")
          set(_compiler_path "${CMAKE_MATCH_1}")
        endif()
      endforeach()
    endif()
    if(_compiler_id STREQUAL "")
      file(GLOB _compiler_cmake_files "${_build_dir}/CMakeFiles/*/CMakeCXXCompiler.cmake")
      foreach(_compiler_file IN LISTS _compiler_cmake_files)
        file(STRINGS "${_compiler_file}" _compiler_id_lines REGEX "CMAKE_CXX_COMPILER_ID")
        foreach(_compiler_id_line IN LISTS _compiler_id_lines)
          if(_compiler_id_line MATCHES "set\\(CMAKE_CXX_COMPILER_ID \"([^\"]+)\"\\)")
            set(_compiler_id "${CMAKE_MATCH_1}")
          endif()
        endforeach()
      endforeach()
    endif()
  endif()

  # Resolve third-party runtime bin/lib directories (for terminal, CTest, and debug environments). When cross-compiling,
  # only the host third-party install prefix is used so target-architecture binaries/libraries never pollute host
  # processes.
  set(_crosscompiling FALSE)
  if(DEFINED CMAKE_CROSSCOMPILING)
    if(CMAKE_CROSSCOMPILING)
      set(_crosscompiling TRUE)
    endif()
  elseif(NOT _build_dir STREQUAL "")
    file(GLOB _system_cmake_files "${_build_dir}/CMakeFiles/*/CMakeSystem.cmake")
    foreach(_system_file IN LISTS _system_cmake_files)
      file(STRINGS "${_system_file}" _cross_lines REGEX "CMAKE_CROSSCOMPILING")
      foreach(_cross_line IN LISTS _cross_lines)
        if(_cross_line MATCHES "set\\(CMAKE_CROSSCOMPILING \"?(TRUE|ON|1)\"?\\)")
          set(_crosscompiling TRUE)
        endif()
      endforeach()
    endforeach()
  endif()

  set(_third_party_install_dir "")
  if(_crosscompiling)
    if(DEFINED PROJECT_THIRD_PARTY_HOST_INSTALL_DIR AND NOT PROJECT_THIRD_PARTY_HOST_INSTALL_DIR STREQUAL "")
      set(_third_party_install_dir "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}")
    endif()
  else()
    if(DEFINED PROJECT_THIRD_PARTY_INSTALL_DIR AND NOT PROJECT_THIRD_PARTY_INSTALL_DIR STREQUAL "")
      set(_third_party_install_dir "${PROJECT_THIRD_PARTY_INSTALL_DIR}")
    elseif(DEFINED PROJECT_THIRD_PARTY_HOST_INSTALL_DIR AND NOT PROJECT_THIRD_PARTY_HOST_INSTALL_DIR STREQUAL "")
      set(_third_party_install_dir "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}")
    elseif(NOT _cache_file STREQUAL "")
      file(STRINGS "${_cache_file}" _cache_third_party REGEX "^PROJECT_THIRD_PARTY_INSTALL_DIR:[^=]*=")
      foreach(_cache_line IN LISTS _cache_third_party)
        if(_cache_line MATCHES "^PROJECT_THIRD_PARTY_INSTALL_DIR:[^=]*=(.+)$")
          set(_third_party_install_dir "${CMAKE_MATCH_1}")
        endif()
      endforeach()
    endif()
  endif()

  set(_install_bindir "")
  set(_install_libdir "")
  if(DEFINED CMAKE_INSTALL_BINDIR AND NOT CMAKE_INSTALL_BINDIR STREQUAL "")
    set(_install_bindir "${CMAKE_INSTALL_BINDIR}")
  endif()
  if(DEFINED CMAKE_INSTALL_LIBDIR AND NOT CMAKE_INSTALL_LIBDIR STREQUAL "")
    set(_install_libdir "${CMAKE_INSTALL_LIBDIR}")
  endif()
  if(NOT _cache_file STREQUAL "")
    if(_install_bindir STREQUAL "")
      file(STRINGS "${_cache_file}" _cache_bindir REGEX "^CMAKE_INSTALL_BINDIR:[^=]*=")
      foreach(_cache_line IN LISTS _cache_bindir)
        if(_cache_line MATCHES "^CMAKE_INSTALL_BINDIR:[^=]*=(.+)$")
          set(_install_bindir "${CMAKE_MATCH_1}")
        endif()
      endforeach()
    endif()
    if(_install_libdir STREQUAL "")
      file(STRINGS "${_cache_file}" _cache_libdir REGEX "^CMAKE_INSTALL_LIBDIR:[^=]*=")
      foreach(_cache_line IN LISTS _cache_libdir)
        if(_cache_line MATCHES "^CMAKE_INSTALL_LIBDIR:[^=]*=(.+)$")
          set(_install_libdir "${CMAKE_MATCH_1}")
        endif()
      endforeach()
    endif()
  endif()

  set(_candidate_bin_dirs)
  set(_candidate_lib_dirs)
  if(NOT _third_party_install_dir STREQUAL "")
    if(NOT _install_bindir STREQUAL "")
      list(APPEND _candidate_bin_dirs "${_third_party_install_dir}/${_install_bindir}")
    endif()
    list(APPEND _candidate_bin_dirs "${_third_party_install_dir}/bin")

    if(NOT _install_libdir STREQUAL "")
      list(APPEND _candidate_lib_dirs "${_third_party_install_dir}/${_install_libdir}")
    endif()
    list(APPEND _candidate_lib_dirs "${_third_party_install_dir}/lib64" "${_third_party_install_dir}/lib")
  endif()

  set(_resolved_bin_dirs)
  foreach(_bin_candidate IN LISTS _candidate_bin_dirs)
    if(NOT _bin_candidate STREQUAL "" AND IS_DIRECTORY "${_bin_candidate}")
      get_filename_component(_bin_candidate_abs "${_bin_candidate}" ABSOLUTE)
      list(APPEND _resolved_bin_dirs "${_bin_candidate_abs}")
    endif()
  endforeach()
  if(_resolved_bin_dirs)
    list(REMOVE_DUPLICATES _resolved_bin_dirs)
  endif()

  set(_resolved_lib_dirs)
  foreach(_lib_candidate IN LISTS _candidate_lib_dirs)
    if(NOT _lib_candidate STREQUAL "" AND IS_DIRECTORY "${_lib_candidate}")
      get_filename_component(_lib_candidate_abs "${_lib_candidate}" ABSOLUTE)
      list(APPEND _resolved_lib_dirs "${_lib_candidate_abs}")
    endif()
  endforeach()
  if(_resolved_lib_dirs)
    list(REMOVE_DUPLICATES _resolved_lib_dirs)
  endif()

  if(CMAKE_HOST_WIN32)
    set(_host_platform "windows")
  elseif(CMAKE_HOST_APPLE)
    set(_host_platform "osx")
  else()
    set(_host_platform "linux")
  endif()

  # Resolve protoc executable for Protobuf VS Code extensions (extension hosts do not inherit terminal/CMake env PATH).
  set(_resolved_protoc "")
  set(_protoc_candidates)
  foreach(_protoc_var ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC Protobuf_PROTOC_EXECUTABLE
                      PROTOBUF_PROTOC_EXECUTABLE)
    if(DEFINED ${_protoc_var} AND NOT "${${_protoc_var}}" STREQUAL "")
      list(APPEND _protoc_candidates "${${_protoc_var}}")
    endif()
  endforeach()
  if(NOT _cache_file STREQUAL "")
    file(STRINGS "${_cache_file}" _cache_protoc REGEX "^(Protobuf|PROTOBUF)_PROTOC_EXECUTABLE:[^=]*=")
    foreach(_cache_line IN LISTS _cache_protoc)
      if(_cache_line MATCHES "^(Protobuf|PROTOBUF)_PROTOC_EXECUTABLE:[^=]*=(.+)$")
        list(APPEND _protoc_candidates "${CMAKE_MATCH_2}")
      endif()
    endforeach()
  endif()
  foreach(_bin_dir IN LISTS _resolved_bin_dirs)
    if(CMAKE_HOST_WIN32)
      list(APPEND _protoc_candidates "${_bin_dir}/protoc.exe")
    endif()
    list(APPEND _protoc_candidates "${_bin_dir}/protoc")
  endforeach()
  foreach(_protoc_candidate IN LISTS _protoc_candidates)
    if(NOT _protoc_candidate STREQUAL ""
       AND EXISTS "${_protoc_candidate}"
       AND NOT IS_DIRECTORY "${_protoc_candidate}")
      get_filename_component(_resolved_protoc "${_protoc_candidate}" ABSOLUTE)
      break()
    endif()
  endforeach()

  # Discover the Python interpreter and helper tools without polluting CMakeCache.txt.
  set(_python_executable "")
  if(DEFINED Python3_EXECUTABLE
     AND NOT Python3_EXECUTABLE STREQUAL ""
     AND EXISTS "${Python3_EXECUTABLE}")
    set(_python_executable "${Python3_EXECUTABLE}")
  elseif(NOT _cache_file STREQUAL "")
    file(STRINGS "${_cache_file}" _cache_python REGEX "^Python3_EXECUTABLE:[^=]*=")
    foreach(_cache_line IN LISTS _cache_python)
      if(_cache_line MATCHES "^Python3_EXECUTABLE:[^=]*=(.+)$")
        if(EXISTS "${CMAKE_MATCH_1}")
          set(_python_executable "${CMAKE_MATCH_1}")
        endif()
      endif()
    endforeach()
  endif()
  if(_python_executable STREQUAL "")
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_EXECUTABLE)
      set(_python_executable "${Python3_EXECUTABLE}")
    endif()
  endif()
  if(_python_executable STREQUAL "")
    message(FATAL_ERROR "[optimize-vscode] No Python interpreter found (looked for python3/python/py).")
  endif()

  find_program(_cpplint NAMES cpplint NO_CACHE)
  set(_clang_format "")
  if(DEFINED PROJECT_TOOL_CLANG_FORMAT
     AND NOT PROJECT_TOOL_CLANG_FORMAT STREQUAL ""
     AND EXISTS "${PROJECT_TOOL_CLANG_FORMAT}")
    set(_clang_format "${PROJECT_TOOL_CLANG_FORMAT}")
  else()
    find_program(_clang_format NAMES clang-format NO_CACHE)
  endif()

  # Assemble the command line and run the optimizer.
  set(_py_args
      "${CMAKE_CURRENT_LIST_DIR}/optimize_vscode_settings.py"
      "--settings"
      "${_workspace_dir}/.vscode/settings.json"
      "--workspace-dir"
      "${_workspace_dir}"
      "--platform"
      "${_host_platform}"
      "--quiet")
  if(NOT _build_dir STREQUAL "")
    list(APPEND _py_args "--build-dir" "${_build_dir}")
  endif()
  if(NOT _compiler_id STREQUAL "")
    list(APPEND _py_args "--compiler-id" "${_compiler_id}")
  endif()
  if(NOT _compiler_path STREQUAL "")
    list(APPEND _py_args "--compiler-path" "${_compiler_path}")
  endif()
  if(_cpplint)
    list(APPEND _py_args "--cpplint" "${_cpplint}")
  endif()
  if(_clang_format)
    list(APPEND _py_args "--clang-format" "${_clang_format}")
  endif()
  if(NOT _resolved_protoc STREQUAL "")
    list(APPEND _py_args "--protoc" "${_resolved_protoc}")
  endif()
  foreach(_bin_dir IN LISTS _resolved_bin_dirs)
    list(APPEND _py_args "--extra-bin-dir" "${_bin_dir}")
  endforeach()
  foreach(_lib_dir IN LISTS _resolved_lib_dirs)
    list(APPEND _py_args "--extra-lib-dir" "${_lib_dir}")
  endforeach()

  set(_status_message
      "[VSCode Integration]
    Workspace       : ${_workspace_dir}
    Build directory : ${_build_dir}
    Compiler        : ${_compiler_id} (${_compiler_path})")
  if(NOT _resolved_protoc STREQUAL "")
    string(APPEND _status_message "\n    Protoc          : ${_resolved_protoc}")
  endif()
  if(_resolved_bin_dirs)
    string(JOIN ", " _bin_dirs_display ${_resolved_bin_dirs})
    string(APPEND _status_message "\n    Extra PATH dirs : ${_bin_dirs_display}")
  endif()
  if(_resolved_lib_dirs)
    string(JOIN ", " _lib_dirs_display ${_resolved_lib_dirs})
    string(APPEND _status_message "\n    Extra lib dirs  : ${_lib_dirs_display}")
  endif()
  message(STATUS "${_status_message}")

  execute_process(COMMAND "${_python_executable}" ${_py_args} RESULT_VARIABLE _result)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "[optimize-vscode] optimizer failed with exit code ${_result}.")
  endif()

  # Clean stale clangd preamble cache (preamble-*.pch) from the system temp directory. clangd serializes each file's
  # preamble to the system temp dir (default --pch-storage=disk) and the files persist across editor restarts. Removing
  # them once per configure keeps the temp dir bounded; clangd regenerates the preamble on demand. Best-effort, never
  # fatal.
  set(_clangd_tmp "")
  if(CMAKE_HOST_WIN32)
    foreach(_tmp_candidate "$ENV{TEMP}" "$ENV{TMP}")
      if(NOT _clangd_tmp
         AND NOT "${_tmp_candidate}" STREQUAL ""
         AND IS_DIRECTORY "${_tmp_candidate}")
        set(_clangd_tmp "${_tmp_candidate}")
      endif()
    endforeach()
  else()
    if(NOT "$ENV{TMPDIR}" STREQUAL "" AND IS_DIRECTORY "$ENV{TMPDIR}")
      set(_clangd_tmp "$ENV{TMPDIR}")
    elseif(IS_DIRECTORY "/tmp")
      set(_clangd_tmp "/tmp")
    endif()
  endif()
  if(_clangd_tmp)
    file(GLOB _stale_preambles "${_clangd_tmp}/preamble-*.pch")
    list(LENGTH _stale_preambles _stale_count)
    if(_stale_count GREATER 0)
      set(_stale_bytes 0)
      foreach(_pch IN LISTS _stale_preambles)
        file(SIZE "${_pch}" _pch_size)
        math(EXPR _stale_bytes "${_stale_bytes} + ${_pch_size}")
      endforeach()
      math(EXPR _stale_mb "${_stale_bytes} / 1048576")
      foreach(_pch IN LISTS _stale_preambles)
        file(REMOVE "${_pch}")
      endforeach()
      message(STATUS "[optimize-vscode] Cleaned ${_stale_count} stale clangd preamble files "
                     "(${_stale_mb} MB) from ${_clangd_tmp}")
    endif()
  endif()
endfunction()

_project_optimize_vscode_settings()
