# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# Helpers shared by service-functions.cmake and component/component-functions.cmake. src/CMakeLists.txt includes
# this file once, before add_subdirectory(component) and before including service-functions.cmake, so both
# function families see these helpers without either layer including the other's function file.

# Routes the files under the entry-point directories out of SOURCES/HEADERS into MAIN_SOURCES/MAIN_HEADERS:
# every file whose directory is ROOT_DIR/<entry directory> (or an absolute entry directory, subdirectories
# included) moves to the main lists, so registration TUs like handle_*_rpc_*.atfw.gen.* (strong symbols named
# after the proto service) compile into the executable instead of the co-linkable implementation library.
# MAIN_DIRECTORIES defaults to "app" when empty. The five list arguments are caller-scope variable names and
# are updated in place.
function(project_service_route_entry_point_files ROOT_DIR SOURCES_VAR HEADERS_VAR MAIN_SOURCES_VAR
                                                 MAIN_HEADERS_VAR MAIN_DIRECTORIES_VAR)
  if(NOT ${MAIN_DIRECTORIES_VAR})
    set(${MAIN_DIRECTORIES_VAR} "app")
  endif()
  # Generated files collected from codegen flows may arrive in native spelling (backslashes on Windows) while
  # callers and file(GLOB_RECURSE) use forward slashes. Normalize every list to forward slashes up front, or
  # directory matching and list(REMOVE_ITEM) compare different spellings of the same file and the duplicates leak
  # into the implementation library.
  string(REPLACE "\\" "/" ROOT_DIR "${ROOT_DIR}")
  unset(__MAIN_DIRECTORIES_ABS)
  foreach(__MAIN_DIRECTORY IN LISTS ${MAIN_DIRECTORIES_VAR})
    string(REGEX REPLACE "/+$" "" __MAIN_DIRECTORY "${__MAIN_DIRECTORY}")
    string(REPLACE "\\" "/" __MAIN_DIRECTORY "${__MAIN_DIRECTORY}")
    if(IS_ABSOLUTE "${__MAIN_DIRECTORY}")
      list(APPEND __MAIN_DIRECTORIES_ABS "${__MAIN_DIRECTORY}")
    else()
      list(APPEND __MAIN_DIRECTORIES_ABS "${ROOT_DIR}/${__MAIN_DIRECTORY}")
    endif()
  endforeach()
  foreach(__KIND SOURCES HEADERS)
    if(__KIND STREQUAL "SOURCES")
      set(__LIST_VAR "${SOURCES_VAR}")
      set(__MAIN_VAR "${MAIN_SOURCES_VAR}")
    else()
      set(__LIST_VAR "${HEADERS_VAR}")
      set(__MAIN_VAR "${MAIN_HEADERS_VAR}")
    endif()
    # Rewrite both lists to the normalized spelling in place so REMOVE_ITEM below compares equal strings and the
    # dual-spelling duplicates from codegen flows collapse.
    unset(__NORMALIZED_FILES)
    foreach(__FILE IN LISTS ${__LIST_VAR})
      string(REPLACE "\\" "/" __FILE "${__FILE}")
      list(APPEND __NORMALIZED_FILES "${__FILE}")
    endforeach()
    set(${__LIST_VAR} "${__NORMALIZED_FILES}")
    unset(__NORMALIZED_FILES)
    foreach(__FILE IN LISTS ${__MAIN_VAR})
      string(REPLACE "\\" "/" __FILE "${__FILE}")
      list(APPEND __NORMALIZED_FILES "${__FILE}")
    endforeach()
    set(${__MAIN_VAR} "${__NORMALIZED_FILES}")
    unset(__NORMALIZED_FILES)
    foreach(__CHECK_FILE IN LISTS ${__LIST_VAR})
      get_filename_component(__CHECK_DIR "${__CHECK_FILE}" DIRECTORY)
      if(NOT IS_ABSOLUTE "${__CHECK_DIR}")
        get_filename_component(__CHECK_DIR "${__CHECK_DIR}" ABSOLUTE BASE_DIR "${ROOT_DIR}")
      endif()
      foreach(__MAIN_DIRECTORY_ABS IN LISTS __MAIN_DIRECTORIES_ABS)
        string(FIND "${__CHECK_DIR}/" "${__MAIN_DIRECTORY_ABS}/" __PREFIX_POS)
        if(__PREFIX_POS EQUAL 0)
          list(APPEND ${__MAIN_VAR} "${__CHECK_FILE}")
          list(REMOVE_ITEM ${__LIST_VAR} "${__CHECK_FILE}")
          break()
        endif()
      endforeach()
    endforeach()
    if(${__LIST_VAR})
      list(REMOVE_DUPLICATES ${__LIST_VAR})
    endif()
  endforeach()
  list(REMOVE_ITEM ${SOURCES_VAR} ${${MAIN_SOURCES_VAR}})
  if(${MAIN_HEADERS_VAR})
    list(REMOVE_ITEM ${HEADERS_VAR} ${${MAIN_HEADERS_VAR}})
    list(REMOVE_DUPLICATES ${MAIN_HEADERS_VAR})
  endif()
  list(REMOVE_DUPLICATES ${MAIN_SOURCES_VAR})
  set(${SOURCES_VAR}
      "${${SOURCES_VAR}}"
      PARENT_SCOPE)
  set(${HEADERS_VAR}
      "${${HEADERS_VAR}}"
      PARENT_SCOPE)
  set(${MAIN_SOURCES_VAR}
      "${${MAIN_SOURCES_VAR}}"
      PARENT_SCOPE)
  set(${MAIN_HEADERS_VAR}
      "${${MAIN_HEADERS_VAR}}"
      PARENT_SCOPE)
endfunction()

# Reads a service property recorded by project_service_declare_instance/project_component_declare_service,
# resolving ALIAS targets (e.g. service::<name>::private) first.
function(project_service_get_target_property TARGET_NAME PROPERTY_NAME OUTPUT_VAR)
  if(NOT TARGET "${TARGET_NAME}")
    message(
      FATAL_ERROR
        "project_service_get_target_property: ${TARGET_NAME} is not a target; it must be created by "
        "project_service_declare_instance/project_component_declare_service before this call")
  endif()
  get_target_property(__REAL_TARGET "${TARGET_NAME}" ALIASED_TARGET)
  if(NOT __REAL_TARGET)
    set(__REAL_TARGET "${TARGET_NAME}")
  endif()
  get_target_property(__PROPERTY_VALUE "${__REAL_TARGET}" "${PROPERTY_NAME}")
  if(NOT __PROPERTY_VALUE)
    message(FATAL_ERROR "project_service_get_target_property: ${TARGET_NAME} has no ${PROPERTY_NAME} property")
  endif()
  set(${OUTPUT_VAR} "${__PROPERTY_VALUE}" PARENT_SCOPE)
endfunction()

# Absolute root directory a service was declared with.
function(project_service_get_target_root_dir TARGET_NAME OUTPUT_VAR)
  project_service_get_target_property("${TARGET_NAME}" SERVICE_ROOT_DIR __SERVICE_ROOT_DIR)
  set(${OUTPUT_VAR} "${__SERVICE_ROOT_DIR}" PARENT_SCOPE)
endfunction()

# Absolute directory consumers add to the include path (INCLUDE_DIR of the declaration, or the root directory).
function(project_service_get_target_include_dir TARGET_NAME OUTPUT_VAR)
  project_service_get_target_property("${TARGET_NAME}" SERVICE_INCLUDE_DIR __SERVICE_INCLUDE_DIR)
  set(${OUTPUT_VAR} "${__SERVICE_INCLUDE_DIR}" PARENT_SCOPE)
endfunction()
