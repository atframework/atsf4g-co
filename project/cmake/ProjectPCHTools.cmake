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

# 获取或按需创建指定 PIC 模式的 PCH 变体目标(仅限非 MSVC)。
#
# 基础 <name>.pch.export 只是元数据容器(非 MSVC 下为 INTERFACE 库,保存 PCH 头文件列表、权重、变体链接目标等属性), 真正的 PCH 由变体目标按需构建: -
# <name>.pch.export.pie: 以 -fPIE 构建,供可执行文件(-fPIE)复用; - <name>.pch.export.pic: 以 -fPIC 构建,供库目标(-fPIC)复用。 GCC/Clang 的 PCH
# 校验要求 -fPIE/-fPIC 与使用方完全一致,两类变体都在消费者 REUSE 时按需创建, 没有消费者的变体不会构建(EXCLUDE_FROM_ALL)。 MSVC 没有 -fPIC/-fPIE 之分,直接复用基础版本即可。
function(project_pch_tool_get_or_create_pch_variant INTERFACE_TARGET_NAME VARIANT_MODE OUT_VARIANT_NAME)
  if(MSVC)
    set("${OUT_VARIANT_NAME}"
        "${INTERFACE_TARGET_NAME}"
        PARENT_SCOPE)
    return()
  endif()

  string(TOLOWER "${VARIANT_MODE}" _pch_variant_suffix)
  set(_pch_variant_name "${INTERFACE_TARGET_NAME}.${_pch_variant_suffix}")
  if(TARGET "${_pch_variant_name}")
    set("${OUT_VARIANT_NAME}"
        "${_pch_variant_name}"
        PARENT_SCOPE)
    return()
  endif()

  get_target_property(_pch_variant_headers "${INTERFACE_TARGET_NAME}" ATFW_TOOL_PCH_HEADER_INCLUDE_PATH)
  get_target_property(_pch_variant_link_targets "${INTERFACE_TARGET_NAME}" ATFW_TOOL_PCH_VARIANT_LINK_TARGETS)
  get_target_property(_pch_variant_folder "${INTERFACE_TARGET_NAME}" FOLDER)

  add_library("${_pch_variant_name}" STATIC "${PROJECT_PCH_TOOL_SOURCE_FILE}")
  set_target_properties(
    "${_pch_variant_name}"
    PROPERTIES EXCLUDE_FROM_ALL TRUE
               POSITION_INDEPENDENT_CODE OFF
               CXX_INCLUDE_WHAT_YOU_USE ""
               CXX_CLANG_TIDY "")
  if(_pch_variant_folder)
    set_target_properties("${_pch_variant_name}" PROPERTIES FOLDER "${_pch_variant_folder}")
  endif()

  if(_pch_variant_headers)
    target_precompile_headers("${_pch_variant_name}" PRIVATE
                              "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${_pch_variant_headers}>>")
  endif()

  if(_pch_variant_link_targets)
    target_link_libraries("${_pch_variant_name}" PRIVATE ${_pch_variant_link_targets})
  endif()

  if(PROJECT_COMMON_PRIVATE_INCLUDE_DIRECTORIES)
    target_include_directories("${_pch_variant_name}" PRIVATE ${PROJECT_COMMON_PRIVATE_INCLUDE_DIRECTORIES})
  endif()
  target_compile_options("${_pch_variant_name}" PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
    target_link_options("${_pch_variant_name}" PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
  endif()

  if(_pch_variant_suffix STREQUAL "pie")
    # 可执行文件使用 -fPIE 且不带 -fvisibility=hidden
    target_compile_options("${_pch_variant_name}" PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fPIE>")
  else()
    # 库目标使用 -fPIC 和 -fvisibility=hidden
    set_target_properties("${_pch_variant_name}" PROPERTIES C_VISIBILITY_PRESET "hidden" CXX_VISIBILITY_PRESET "hidden")
    target_compile_options("${_pch_variant_name}" PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fPIC>")
  endif()

  set("${OUT_VARIANT_NAME}"
      "${_pch_variant_name}"
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

    if(NOT __project_pch_tool_FOLDER)
      set(__project_pch_tool_FOLDER "${PROJECT_NAME}/tools/pch")
    endif()

    # 非 MSVC 下 <name>.pch.export 只是元数据容器(INTERFACE,保存 PCH 头文件列表、权重、变体链接目标等属性),PCH 由变体目标按需构建: - <name>.pch.export.pie:
    # -fPIE,供可执行文件(-fPIE)复用; - <name>.pch.export.pic: -fPIC,供库目标(-fPIC)复用。 MSVC 没有 -fPIC/-fPIE 之分,继续由 <name>.pch.export
    # 直接构建 PCH。
    set(_pch_variant_link_targets ${__project_pch_tool_REUSE_FROM_TARGET})
    if(NOT _target_type STREQUAL "EXECUTABLE")
      list(APPEND _pch_variant_link_targets "${TARGET_NAME}")
    endif()
    list(APPEND _pch_variant_link_targets "${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")

    if(MSVC)
      if(BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
        add_library("${_interface_target_name}" STATIC "${PROJECT_PCH_TOOL_SOURCE_FILE}")
      else()
        add_library("${_interface_target_name}" SHARED "${PROJECT_PCH_TOOL_SOURCE_FILE}")
      endif()
      if(_target_type STREQUAL "EXECUTABLE")
        add_dependencies("${_interface_target_name}" "${TARGET_NAME}")
      else()
        target_link_libraries("${_interface_target_name}" PRIVATE "${TARGET_NAME}")
      endif()
      # The merged pch header list below also contains public pch headers inherited from reuse candidates, so propagate
      # their usage requirements (include directories and build order).
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
    else()
      add_library("${_interface_target_name}" INTERFACE)
      set_target_properties("${_interface_target_name}" PROPERTIES ATFW_TOOL_PCH_VARIANT_LINK_TARGETS
                                                                   "${_pch_variant_link_targets}")
    endif()

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
    if(MSVC)
      target_precompile_headers("${_interface_target_name}" PRIVATE
                                "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__current_pch_header_include_path}>>")
    endif()
    return()
  else()
    list(REMOVE_DUPLICATES __current_pch_header_include_path)
    set(_interface_target_name "${TARGET_NAME}")
  endif()

  # reuse模式
  get_target_property(_target_type "${TARGET_NAME}" TYPE)
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

    if(_target_type STREQUAL "EXECUTABLE")
      # 可执行文件使用 -fPIE,复用 <name>.pch.export.pie 版本
      project_pch_tool_get_or_create_pch_variant("${_current_select_target}" "PIE" _current_pie_pch_target)
      target_precompile_headers("${_interface_target_name}" REUSE_FROM "${_current_pie_pch_target}")
      add_dependencies("${_interface_target_name}" "${_current_pie_pch_target}")
    else()
      # 库目标使用 -fPIC,复用 <name>.pch.export.pic 版本;若没有库目标复用,该版本不会构建
      project_pch_tool_get_or_create_pch_variant("${_current_select_target}" "PIC" _current_pic_pch_target)
      target_precompile_headers("${_interface_target_name}" REUSE_FROM "${_current_pic_pch_target}")
      add_dependencies("${_interface_target_name}" "${_current_pic_pch_target}")
    endif()
  endif()
endfunction()
