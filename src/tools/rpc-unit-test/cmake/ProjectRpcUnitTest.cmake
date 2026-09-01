# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# Helper functions and support targets for component RPC unit tests.
#
# This file is included by src/tools/rpc-unit-test/CMakeLists.txt, which is added before `component` in
# src/CMakeLists.txt (gated by `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST`), so every component can call
# `project_add_rpc_unit_test` from its own CMakeLists.txt.
#
# All test executables use only the atframe_utils private test framework (CASE_TEST). No GTest target is linked and no
# framework-switch macro is defined, even when a GTest target exists in the build.

if(DEFINED PROJECT_RPC_UNIT_TEST_CMAKE_INCLUDED)
  return()
endif()
set(PROJECT_RPC_UNIT_TEST_CMAKE_INCLUDED TRUE)

# Cache-internal so project_add_rpc_unit_test sees it when called from other directory scopes (e.g.
# src/server_frame/test), not just from src/tools/rpc-unit-test itself.
set(PROJECT_RPC_UNIT_TEST_FRAME_DIR
    "${ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR}/test"
    CACHE INTERNAL "atframe_utils private test framework directory")

# Centralized support targets: the private framework main and frame implementation are compiled once and reused by every
# test executable via $<TARGET_OBJECTS:...>, so components do not repeatedly compile the same frame sources.
if(NOT TARGET ${PROJECT_NAME}-rpc-unit-test-private-main)
  add_library(${PROJECT_NAME}-rpc-unit-test-private-main OBJECT "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}/app/main.cpp")
  target_include_directories(${PROJECT_NAME}-rpc-unit-test-private-main PRIVATE "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}")
  target_link_libraries(${PROJECT_NAME}-rpc-unit-test-private-main PRIVATE ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})
  target_compile_options(${PROJECT_NAME}-rpc-unit-test-private-main PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  set_property(TARGET ${PROJECT_NAME}-rpc-unit-test-private-main PROPERTY FOLDER "${PROJECT_NAME}/test")
endif()

if(NOT TARGET ${PROJECT_NAME}-rpc-unit-test-private-frame)
  add_library(
    ${PROJECT_NAME}-rpc-unit-test-private-frame OBJECT "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}/frame/test_case_base.cpp"
                                                       "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}/frame/test_manager.cpp")
  target_include_directories(${PROJECT_NAME}-rpc-unit-test-private-frame PRIVATE "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}")
  target_link_libraries(${PROJECT_NAME}-rpc-unit-test-private-frame PRIVATE ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})
  target_compile_options(${PROJECT_NAME}-rpc-unit-test-private-frame PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  set_property(TARGET ${PROJECT_NAME}-rpc-unit-test-private-frame PROPERTY FOLDER "${PROJECT_NAME}/test")
endif()

#[[
project_add_rpc_unit_test(
  TARGET <target>                   # required, executable target name
  COMPONENT <component-name>        # required, used for stable labels/folder, never guessed from paths
  CATEGORY <component|sdk|service>  # optional label category prefix, defaults to "component"
  SOURCES <a.cpp> [<b.cpp> ...]     # required, test sources of this component
  LINK_LIBRARIES <targets...>       # optional, component SDK/protocol/implementation targets
  FEATURES <SS DNS CS DB UUID RESOURCE ROUTER ORBIT HPA...> # optional, validated feature tags
  LABELS <extra ctest labels...>
  TIMEOUT <seconds>                 # ctest process-level timeout, must cover the worst serial-case sum
  CONFIG <file>                     # optional extra config file copied next to the executable working directory
  WORKING_DIRECTORY <dir>           # optional, defaults to a per-target directory inside the caller build tree
  ENVIRONMENT <VAR=value ...>       # optional extra ctest environment
)
]]
function(project_add_rpc_unit_test)
  set(PROJECT_RPC_UNIT_TEST_KNOWN_FEATURES
      SS
      DNS
      CS
      DB
      UUID
      RESOURCE
      ROUTER
      ORBIT
      HPA
      TELEMETRY)
  cmake_parse_arguments(PROJECT_RPC_UNIT_TEST "" "TARGET;COMPONENT;CATEGORY;TIMEOUT;CONFIG;WORKING_DIRECTORY"
                        "SOURCES;LINK_LIBRARIES;FEATURES;LABELS;ENVIRONMENT" ${ARGN})

  if(NOT PROJECT_RPC_UNIT_TEST_TARGET)
    message(FATAL_ERROR "project_add_rpc_unit_test: TARGET is required")
  endif()
  if(NOT PROJECT_RPC_UNIT_TEST_COMPONENT)
    message(FATAL_ERROR "project_add_rpc_unit_test: COMPONENT is required")
  endif()
  if(NOT PROJECT_RPC_UNIT_TEST_SOURCES)
    message(FATAL_ERROR "project_add_rpc_unit_test: SOURCES is required")
  endif()

  foreach(PROJECT_RPC_UNIT_TEST_FEATURE IN LISTS PROJECT_RPC_UNIT_TEST_FEATURES)
    if(NOT PROJECT_RPC_UNIT_TEST_FEATURE IN_LIST PROJECT_RPC_UNIT_TEST_KNOWN_FEATURES)
      message(FATAL_ERROR "project_add_rpc_unit_test: unknown feature ${PROJECT_RPC_UNIT_TEST_FEATURE}, available: "
                          "${PROJECT_RPC_UNIT_TEST_KNOWN_FEATURES}")
    endif()
  endforeach()
  if(PROJECT_RPC_UNIT_TEST_FEATURES MATCHES "(^|;)ORBIT(;|$)" AND NOT TARGET ${PROJECT_NAME}-rpc-unit-test-orbit)
    message(FATAL_ERROR "project_add_rpc_unit_test: feature ORBIT requires target ${PROJECT_NAME}-rpc-unit-test-orbit, "
                        "which is only available when the Orbit client SDK is built")
  endif()

  add_executable(
    ${PROJECT_RPC_UNIT_TEST_TARGET}
    ${PROJECT_RPC_UNIT_TEST_SOURCES} $<TARGET_OBJECTS:${PROJECT_NAME}-rpc-unit-test-private-main>
    $<TARGET_OBJECTS:${PROJECT_NAME}-rpc-unit-test-private-frame>)
  target_include_directories(${PROJECT_RPC_UNIT_TEST_TARGET} PRIVATE "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}")
  target_link_libraries(
    ${PROJECT_RPC_UNIT_TEST_TARGET}
    PRIVATE ${PROJECT_NAME}::rpc-unit-test ${PROJECT_SERVER_FRAME_LIB_LINK} ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}
            ${PROJECT_RPC_UNIT_TEST_LINK_LIBRARIES})
  if(PROJECT_RPC_UNIT_TEST_FEATURES MATCHES "(^|;)ORBIT(;|$)")
    target_link_libraries(${PROJECT_RPC_UNIT_TEST_TARGET} PRIVATE ${PROJECT_NAME}::rpc-unit-test-orbit)
  endif()
  target_compile_options(${PROJECT_RPC_UNIT_TEST_TARGET} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  project_tool_split_target_debug_sybmol(${PROJECT_RPC_UNIT_TEST_TARGET})
  project_tool_set_target_runtime_output_directory("${PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY}"
                                                   ${PROJECT_RPC_UNIT_TEST_TARGET} WITH_TARGET_RPATH WITH_ARCHIVE_RPATH)

  # Reuse PCH from dependencies. The support library reuses the server_frame PCH, so it resolves to the same pch
  # interface target; linked component SDK/protocol targets are candidates as well and the pch tool picks the one with
  # the highest reuse weight.
  set(PROJECT_RPC_UNIT_TEST_PCH_REUSE_TARGETS "${PROJECT_NAME}-rpc-unit-test" "${PROJECT_SERVER_FRAME_LIB_LINK}")
  foreach(PROJECT_RPC_UNIT_TEST_PCH_REUSE_CANDIDATE IN LISTS PROJECT_RPC_UNIT_TEST_LINK_LIBRARIES)
    if(NOT TARGET ${PROJECT_RPC_UNIT_TEST_PCH_REUSE_CANDIDATE})
      continue()
    endif()
    get_target_property(__RESOLVE_ALIAS_TARGET "${PROJECT_RPC_UNIT_TEST_PCH_REUSE_CANDIDATE}" ALIASED_TARGET)
    if(__RESOLVE_ALIAS_TARGET)
      list(APPEND PROJECT_RPC_UNIT_TEST_PCH_REUSE_TARGETS "${__RESOLVE_ALIAS_TARGET}")
    else()
      list(APPEND PROJECT_RPC_UNIT_TEST_PCH_REUSE_TARGETS "${PROJECT_RPC_UNIT_TEST_PCH_REUSE_CANDIDATE}")
    endif()
    unset(__RESOLVE_ALIAS_TARGET)
  endforeach()
  project_pch_tool_set_precompile_headers(${PROJECT_RPC_UNIT_TEST_TARGET} FOLDER "${PROJECT_NAME}/test"
                                          REUSE_FROM_TARGET ${PROJECT_RPC_UNIT_TEST_PCH_REUSE_TARGETS})

  set_target_properties(
    ${PROJECT_RPC_UNIT_TEST_TARGET}
    PROPERTIES INSTALL_RPATH_USE_LINK_PATH YES
               BUILD_WITH_INSTALL_RPATH NO
               BUILD_RPATH_USE_ORIGIN YES)
  set_property(TARGET ${PROJECT_RPC_UNIT_TEST_TARGET} PROPERTY FOLDER "${PROJECT_NAME}/test")

  project_setup_runtime_post_build_bash(${PROJECT_RPC_UNIT_TEST_TARGET}
                                        PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_LIBRARY_BASH)
  project_setup_runtime_post_build_pwsh(${PROJECT_RPC_UNIT_TEST_TARGET}
                                        PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_LIBRARY_PWSH)

  if(NOT PROJECT_RPC_UNIT_TEST_WORKING_DIRECTORY)
    set(PROJECT_RPC_UNIT_TEST_WORKING_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/rpc-unit-test-workdir/${PROJECT_RPC_UNIT_TEST_TARGET}")
  endif()
  file(MAKE_DIRECTORY "${PROJECT_RPC_UNIT_TEST_WORKING_DIRECTORY}")

  if(PROJECT_RPC_UNIT_TEST_CONFIG)
    add_custom_command(
      TARGET ${PROJECT_RPC_UNIT_TEST_TARGET}
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_RPC_UNIT_TEST_CONFIG}"
              "${PROJECT_RPC_UNIT_TEST_WORKING_DIRECTORY}/")
  endif()

  add_test(NAME ${PROJECT_RPC_UNIT_TEST_TARGET}.unit COMMAND $<TARGET_FILE:${PROJECT_RPC_UNIT_TEST_TARGET}>)

  # The user-supplied LABELS argument is PROJECT_RPC_UNIT_TEST_LABELS after cmake_parse_arguments; build the final label
  # list in a separate variable so the argument survives.
  if(NOT PROJECT_RPC_UNIT_TEST_CATEGORY)
    set(PROJECT_RPC_UNIT_TEST_CATEGORY "component")
  endif()
  set(PROJECT_RPC_UNIT_TEST_ALL_LABELS ${PROJECT_NAME} unit rpc-unit-test
                                       "${PROJECT_RPC_UNIT_TEST_CATEGORY}:${PROJECT_RPC_UNIT_TEST_COMPONENT}")
  foreach(PROJECT_RPC_UNIT_TEST_FEATURE IN LISTS PROJECT_RPC_UNIT_TEST_FEATURES)
    string(TOLOWER "${PROJECT_RPC_UNIT_TEST_FEATURE}" PROJECT_RPC_UNIT_TEST_FEATURE_LOWER)
    list(APPEND PROJECT_RPC_UNIT_TEST_ALL_LABELS "feature:${PROJECT_RPC_UNIT_TEST_FEATURE_LOWER}")
  endforeach()
  list(APPEND PROJECT_RPC_UNIT_TEST_ALL_LABELS ${PROJECT_RPC_UNIT_TEST_LABELS})

  if(NOT PROJECT_RPC_UNIT_TEST_TIMEOUT)
    set(PROJECT_RPC_UNIT_TEST_TIMEOUT 120)
  endif()

  set_tests_properties(
    ${PROJECT_RPC_UNIT_TEST_TARGET}.unit
    PROPERTIES LABELS "${PROJECT_RPC_UNIT_TEST_ALL_LABELS}" TIMEOUT "${PROJECT_RPC_UNIT_TEST_TIMEOUT}"
               WORKING_DIRECTORY "${PROJECT_RPC_UNIT_TEST_WORKING_DIRECTORY}")

  # Windows: CTest prepends the test, project-runtime, and third-party DLL directories to PATH. The test output remains
  # separate from the publish tree and does not need copied DLLs.
  if(WIN32)
    set(PROJECT_RPC_UNIT_TEST_PATH_MODIFICATIONS "PATH=path_list_prepend:${PROJECT_TEST_RUNTIME_OUTPUT_DIRECTORY}")
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
      list(APPEND PROJECT_RPC_UNIT_TEST_PATH_MODIFICATIONS "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
    if(PROJECT_THIRD_PARTY_INSTALL_DIR)
      list(APPEND PROJECT_RPC_UNIT_TEST_PATH_MODIFICATIONS
           "PATH=path_list_prepend:${PROJECT_THIRD_PARTY_INSTALL_DIR}/bin")
    endif()
    set_property(
      TEST ${PROJECT_RPC_UNIT_TEST_TARGET}.unit
      APPEND
      PROPERTY ENVIRONMENT_MODIFICATION "${PROJECT_RPC_UNIT_TEST_PATH_MODIFICATIONS}")
  endif()

  # Per-target working directory for the runtime (preserves per-fixture isolation when ctest runs targets in parallel).
  # The runtime reads RPC_UNIT_TEST_WORKDIR before falling back to its build-tree default.
  set_property(
    TEST ${PROJECT_RPC_UNIT_TEST_TARGET}.unit
    APPEND
    PROPERTY ENVIRONMENT "RPC_UNIT_TEST_WORKDIR=${PROJECT_RPC_UNIT_TEST_WORKING_DIRECTORY}")

  if(PROJECT_RPC_UNIT_TEST_ENVIRONMENT)
    set_property(
      TEST ${PROJECT_RPC_UNIT_TEST_TARGET}.unit
      APPEND
      PROPERTY ENVIRONMENT "${PROJECT_RPC_UNIT_TEST_ENVIRONMENT}")
  endif()
endfunction()
