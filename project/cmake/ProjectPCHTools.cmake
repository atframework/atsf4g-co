# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

include_guard(GLOBAL)

define_property(
  TARGET
  PROPERTY ATFW_TOOL_PCH_HEADER_INCLUDE_PATH
  BRIEF_DOCS "A file list of precompiled headers to be included for the target"
  FULL_DOCS "A file list of precompiled headers to be included for the target")

define_property(
  TARGET
  PROPERTY ATFW_TOOL_PCH_WEIGHT
  BRIEF_DOCS "PCH weight for the target"
  FULL_DOCS "When building new pch target, we will sum all ATFW_TOOL_PCH_WEIGHT of dependency target")

define_property(
  TARGET
  PROPERTY ATFW_TOOL_PCH_REUSE_WEIGHT
  BRIEF_DOCS "PCH reuse weight for the target"
  FULL_DOCS "When selecting pch reuse from, we will select the target with the highest weight")

define_property(
  TARGET
  PROPERTY ATFW_TOOL_PCH_INTERFACE_TARGET_NAME
  BRIEF_DOCS "PCH interface target name"
  FULL_DOCS "The interface target name used to reuse PCH files")

set(PROJECT_PCH_TOOL_SOURCE_FILE "${CMAKE_CURRENT_BINARY_DIR}/project_pch_tool_empty_source.cpp")
set(PROJECT_PCH_TOOL_GENERATED_DIR "${CMAKE_BINARY_DIR}/_generated/pch")
file(TOUCH "${PROJECT_PCH_TOOL_SOURCE_FILE}")

if(PROJECT_ENABLE_PRECOMPILE_HEADERS AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
  list(APPEND PROJECT_COMMON_PRIVATE_INCLUDE_DIRECTORIES "${PROJECT_PCH_TOOL_GENERATED_DIR}")
endif()

function(project_pch_tool_get_pch_weight TARGET_NAME OUTPUT_VAR_NAME)
  if(NOT PROJECT_ENABLE_PRECOMPILE_HEADERS)
    set(${OUTPUT_VAR_NAME}
        0
        PARENT_SCOPE)
    return()
  endif()
  if(CMAKE_VERSION VERSION_LESS "3.16")
    set(${OUTPUT_VAR_NAME}
        0
        PARENT_SCOPE)
    return()
  endif()

  get_target_property(_weight "${TARGET_NAME}" ATFW_TOOL_PCH_WEIGHT)
  if(NOT _weight)
    set(${OUTPUT_VAR_NAME}
        0
        PARENT_SCOPE)
    return()
  else()
    set(${OUTPUT_VAR_NAME}
        ${_weight}
        PARENT_SCOPE)
    return()
  endif()
endfunction()

# project_pch_tool_increase_pch_weight(OUTPUT_VAR_NAME WEIGHT_RATIO [FILE PATH...])
function(project_pch_tool_increase_pch_weight OUTPUT_VAR_NAME WEIGHT_RATIO)
  if(NOT PROJECT_ENABLE_PRECOMPILE_HEADERS)
    return()
  endif()
  if(CMAKE_VERSION VERSION_LESS "3.16")
    return()
  endif()
  if(WEIGHT_RATIO LESS_EQUAL 0)
    return()
  endif()

  if(DEFINED ${OUTPUT_VAR_NAME})
    set(_pch_weight ${${OUTPUT_VAR_NAME}})
  else()
    set(_pch_weight 0)
  endif()

  foreach(_calc_file ${ARGN})
    if(EXISTS "${_calc_file}")
      file(SIZE "${_calc_file}" _calc_file_size)
      math(EXPR _pch_weight "${_pch_weight} + ${_calc_file_size} * ${WEIGHT_RATIO}")
    endif()
  endforeach()

  set(${OUTPUT_VAR_NAME}
      ${_pch_weight}
      PARENT_SCOPE)
endfunction()

# project_pch_tool_collect_pch_weight(TARGET_NAME OUTPUT_VAR_NAME [target names...])
function(project_pch_tool_collect_pch_weight TARGET_NAME OUTPUT_VAR_NAME)
  if(NOT PROJECT_ENABLE_PRECOMPILE_HEADERS)
    set(${OUTPUT_VAR_NAME}
        0
        PARENT_SCOPE)
    return()
  endif()
  if(CMAKE_VERSION VERSION_LESS "3.16")
    set(${OUTPUT_VAR_NAME}
        0
        PARENT_SCOPE)
    return()
  endif()

  if(DEFINED ${OUTPUT_VAR_NAME})
    set(_current_weight ${${OUTPUT_VAR_NAME}})
  else()
    set(_current_weight 0)
  endif()

  foreach(_target_name ${ARGN})
    if(NOT TARGET ${_target_name})
      continue()
    endif()
    project_pch_tool_get_pch_weight("${_target_name}" _target_weight)
    math(EXPR _current_weight "${_current_weight} + ${_target_weight}")
  endforeach()

  set("${OUTPUT_VAR_NAME}"
      ${_current_weight}
      PARENT_SCOPE)
endfunction()

# project_pch_tool_collect_precompile_header_include_path(TARGET_NAME OUTPUT_VAR_NAME [OPTIONS...])
function(project_pch_tool_collect_precompile_header_include_path TARGET_NAME OUTPUT_VAR_NAME)
  if(NOT PROJECT_ENABLE_PRECOMPILE_HEADERS)
    set(${OUTPUT_VAR_NAME}
        ""
        PARENT_SCOPE)
    return()
  endif()
  if(CMAKE_VERSION VERSION_LESS "3.16")
    set(${OUTPUT_VAR_NAME}
        ""
        PARENT_SCOPE)
    return()
  endif()

  set(optionArgs "")
  set(oneValueArgs)
  set(multiValueArgs PRECOMPILE_HEADER REUSE_FROM_TARGET)
  cmake_parse_arguments(__project_pch_tool "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(_header_files)
  if(__project_pch_tool_PRECOMPILE_HEADER)
    list(APPEND _header_files ${__project_pch_tool_PRECOMPILE_HEADER})
  endif()

  foreach(_reuse_target ${__project_pch_tool_REUSE_FROM_TARGET})
    get_target_property(_reuse_target_pch_headers "${_reuse_target}" ATFW_TOOL_PCH_HEADER_INCLUDE_PATH)
    if(_reuse_target_pch_headers)
      list(APPEND _header_files ${_reuse_target_pch_headers})
    endif()
  endforeach()

  set("${OUTPUT_VAR_NAME}"
      "${_header_files}"
      PARENT_SCOPE)
endfunction()

# project_pch_tool_set_precompile_headers(TARGET_NAME [OPTIONS...])
function(project_pch_tool_set_precompile_headers TARGET_NAME)
  if(NOT PROJECT_ENABLE_PRECOMPILE_HEADERS)
    return()
  endif()
  if(CMAKE_VERSION VERSION_LESS "3.16")
    return()
  endif()

  set(optionArgs "")
  set(oneValueArgs PCH_INTERFACE_TARGET_NAME PCH_INIT_WEIGHT_RATIO FOLDER)
  set(multiValueArgs PUBLIC_PRECOMPILE_HEADER PRIVATE_PRECOMPILE_HEADER PROTOCOL_PRECOMPILE_HEADER REUSE_FROM_TARGET
                     ADDITIONAL_PCH_WEIGHT_TARGET)
  cmake_parse_arguments(__project_pch_tool "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # 收集所有的pch 头文件
  unset(__current_pch_header_include_path)
  project_pch_tool_collect_precompile_header_include_path(
    "${TARGET_NAME}" __current_pch_header_include_path PRECOMPILE_HEADER ${__project_pch_tool_PUBLIC_PRECOMPILE_HEADER}
    REUSE_FROM_TARGET ${__project_pch_tool_REUSE_FROM_TARGET})

  if(__project_pch_tool_PCH_INIT_WEIGHT_RATIO)
    set(__current_pch_weight ${__project_pch_tool_PCH_INIT_WEIGHT_RATIO})
  else()
    set(__current_pch_weight 0)
  endif()
  project_pch_tool_collect_pch_weight("${TARGET_NAME}" __current_pch_weight ${__project_pch_tool_REUSE_FROM_TARGET}
                                      ${__project_pch_tool_ADDITIONAL_PCH_WEIGHT_TARGET})

  # protocol pch 需要包一层 include guard，屏蔽 warning
  unset(__project_pch_tool_protocol_include_path)
  if(__project_pch_tool_PROTOCOL_PRECOMPILE_HEADER)
    list(SORT __project_pch_tool_PROTOCOL_PRECOMPILE_HEADER)
    set(__project_pch_tool_protocol_include_path "${PROJECT_COMMON_PRIVATE_INCLUDE_DIRECTORIES}")
    set(__project_pch_tool_protocol_include_file_prefix
        "${__project_pch_tool_protocol_include_path}/atfw.protocol_pch.${TARGET_NAME}")
    string(TIMESTAMP __project_pch_tool_copyright_year "%Y")
    set(__pch_include_content "// Copyright ${__project_pch_tool_copyright_year} atframework\n")
    string(
      APPEND
      __pch_include_content
      [=[// Licensed under the Apache License, Version 2.0 (the \"License\");

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

]=])

    foreach(_pch_file ${__project_pch_tool_PROTOCOL_PRECOMPILE_HEADER})
      if(_pch_file MATCHES "^[<\"]")
        string(APPEND __pch_include_content "#include ${_pch_file}\n")
      else()
        string(APPEND __pch_include_content "#include \"${_pch_file}\"\n")
      endif()
    endforeach()

    string(APPEND __pch_include_content "\n")
    string(
      APPEND
      __pch_include_content
      [=[// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

]=])

    file(WRITE "${__project_pch_tool_protocol_include_file_prefix}.new.h" "${__pch_include_content}")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${__project_pch_tool_protocol_include_file_prefix}.new.h"
              "${__project_pch_tool_protocol_include_file_prefix}.inc.gen.h")

    list(PREPEND __project_pch_tool_PUBLIC_PRECOMPILE_HEADER "atfw.protocol_pch.${TARGET_NAME}.inc.gen.h")
  endif()

  # 本地有额外的pch文件的模式
  if(__project_pch_tool_PUBLIC_PRECOMPILE_HEADER OR __project_pch_tool_PRIVATE_PRECOMPILE_HEADER)
    get_target_property(_target_type "${TARGET_NAME}" TYPE)
    if(__project_pch_tool_PCH_INTERFACE_TARGET_NAME)
      set(_interface_target_name "${__project_pch_tool_PCH_INTERFACE_TARGET_NAME}")
    else()
      set(_interface_target_name "${TARGET_NAME}.pch.export")
    endif()

    if(__project_pch_tool_PUBLIC_PRECOMPILE_HEADER)
      list(APPEND __current_pch_header_include_path ${__project_pch_tool_PUBLIC_PRECOMPILE_HEADER})
    endif()
    list(REMOVE_DUPLICATES __current_pch_header_include_path)
    if(__project_pch_tool_PRIVATE_PRECOMPILE_HEADER)
      set(__current_pch_header_all_include_path ${__current_pch_header_include_path}
                                                ${__project_pch_tool_PRIVATE_PRECOMPILE_HEADER})
      list(REMOVE_DUPLICATES __current_pch_header_all_include_path)
    else()
      set(__current_pch_header_all_include_path ${__current_pch_header_include_path})
    endif()

    if(BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
      add_library("${_interface_target_name}" STATIC "${PROJECT_PCH_TOOL_SOURCE_FILE}")
    else()
      add_library("${_interface_target_name}" SHARED "${PROJECT_PCH_TOOL_SOURCE_FILE}")
    endif()
    if(NOT __project_pch_tool_FOLDER)
      set(__project_pch_tool_FOLDER "${PROJECT_NAME}/tools/pch")
    endif()

    if(_target_type STREQUAL "EXECUTABLE")
      add_dependencies("${_interface_target_name}" "${TARGET_NAME}")
    else()
      target_link_libraries("${_interface_target_name}" PRIVATE "${TARGET_NAME}")
    endif()
    # The merged pch header list below also contains public pch headers inherited from reuse
    # candidates, so propagate their usage requirements (include directories and build order).
    foreach(__project_pch_tool_reuse_include_target ${__project_pch_tool_REUSE_FROM_TARGET})
      if(TARGET ${__project_pch_tool_reuse_include_target})
        target_link_libraries("${_interface_target_name}" PRIVATE ${__project_pch_tool_reuse_include_target})
      endif()
    endforeach()
    target_link_libraries("${_interface_target_name}" PRIVATE "${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
    set_target_properties("${_interface_target_name}" PROPERTIES C_VISIBILITY_PRESET "hidden" CXX_VISIBILITY_PRESET
                                                                                              "hidden")
    target_compile_options("${_interface_target_name}" PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
    if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
      target_link_options("${_interface_target_name}" PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
    endif()
    if(PROJECT_COMMON_PRIVATE_INCLUDE_DIRECTORIES)
      target_include_directories("${_interface_target_name}" PRIVATE ${PROJECT_COMMON_PRIVATE_INCLUDE_DIRECTORIES})
    endif()
    target_compile_options("${_interface_target_name}" PRIVATE ${PROJECT_COMMON_PROTOCOL_SOURCE_COMPILE_OPTIONS})

    set_target_properties(
      "${_interface_target_name}"
      PROPERTIES FOLDER "${__project_pch_tool_FOLDER}"
                 CXX_INCLUDE_WHAT_YOU_USE ""
                 CXX_CLANG_TIDY ""
                 ATFW_TOOL_PCH_INTERFACE_TARGET_NAME "${_interface_target_name}"
                 ATFW_TOOL_PCH_HEADER_INCLUDE_PATH "${__current_pch_header_include_path}"
                 ATFW_TOOL_PCH_WEIGHT "${__current_pch_weight}"
                 ATFW_TOOL_PCH_REUSE_WEIGHT "${__current_pch_weight}")
    set_target_properties(
      "${TARGET_NAME}"
      PROPERTIES ATFW_TOOL_PCH_INTERFACE_TARGET_NAME "${_interface_target_name}"
                 ATFW_TOOL_PCH_HEADER_INCLUDE_PATH "${__current_pch_header_include_path}"
                 ATFW_TOOL_PCH_WEIGHT "${__current_pch_weight}"
                 ATFW_TOOL_PCH_REUSE_WEIGHT "${__current_pch_weight}")

    if(_target_type STREQUAL "INTERFACE_LIBRARY")
      if(__project_pch_tool_protocol_include_path)
        target_link_libraries("${TARGET_NAME}" INTERFACE "${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
      endif()
    else()
      if(__project_pch_tool_protocol_include_path)
        target_link_libraries("${TARGET_NAME}" PUBLIC "${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
      endif()

      target_precompile_headers(
        "${TARGET_NAME}" PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__current_pch_header_all_include_path}>>")
    endif()
    target_precompile_headers("${_interface_target_name}" PRIVATE
                              "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__current_pch_header_include_path}>>")
    return()
  else()
    list(REMOVE_DUPLICATES __current_pch_header_include_path)
    set(_interface_target_name "${TARGET_NAME}")
  endif()

  # reuse模式
  set(_current_select_target)
  set(_current_select_weight 0)
  foreach(_reuse_target_name ${__project_pch_tool_REUSE_FROM_TARGET})
    set(_reuse_target_pch_weight 0)
    get_target_property(_reuse_target_pch_weight "${_reuse_target_name}" ATFW_TOOL_PCH_REUSE_WEIGHT)
    if(_reuse_target_pch_weight AND _reuse_target_pch_weight GREATER _current_select_weight)
      get_target_property(_current_select_target "${_reuse_target_name}" ATFW_TOOL_PCH_INTERFACE_TARGET_NAME)
      set(_current_select_weight "${_reuse_target_pch_weight}")
    endif()
  endforeach()

  if(_current_select_target)
    set_target_properties(
      "${_interface_target_name}"
      PROPERTIES ATFW_TOOL_PCH_INTERFACE_TARGET_NAME "${_current_select_target}"
                 ATFW_TOOL_PCH_HEADER_INCLUDE_PATH "${__current_pch_header_include_path}"
                 ATFW_TOOL_PCH_WEIGHT "${__current_pch_weight}"
                 ATFW_TOOL_PCH_REUSE_WEIGHT "${_current_select_weight}")

    target_precompile_headers("${_interface_target_name}" REUSE_FROM "${_current_select_target}")
    add_dependencies("${_interface_target_name}" "${_current_select_target}")
  endif()
endfunction()
