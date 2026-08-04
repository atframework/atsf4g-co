# Helper functions and support targets for component RPC unit tests.
#
# This file is included by src/tools/rpc-unit-test/CMakeLists.txt, which is added before `component` in
# src/CMakeLists.txt (gated by `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST`), so every component can call
# `project_add_rpc_unit_test` from its own CMakeLists.txt.
#
# All test executables use only the atframe_utils private test framework (CASE_TEST). No GTest target is linked and
# no framework-switch macro is defined, even when a GTest target exists in the build.

if(DEFINED PROJECT_RPC_UNIT_TEST_CMAKE_INCLUDED)
  return()
endif()
set(PROJECT_RPC_UNIT_TEST_CMAKE_INCLUDED TRUE)

set(PROJECT_RPC_UNIT_TEST_FRAME_DIR "${ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR}/test")

# Centralized support targets: the private framework main and frame implementation are compiled once and reused by
# every test executable via $<TARGET_OBJECTS:...>, so components do not repeatedly compile the same frame sources.
if(NOT TARGET atf4g-co-rpc-unit-test-private-main)
  add_library(atf4g-co-rpc-unit-test-private-main OBJECT
              "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}/app/main.cpp")
  target_include_directories(atf4g-co-rpc-unit-test-private-main PRIVATE "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}")
  target_link_libraries(atf4g-co-rpc-unit-test-private-main PRIVATE ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})
  target_compile_options(atf4g-co-rpc-unit-test-private-main PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  set_property(TARGET atf4g-co-rpc-unit-test-private-main PROPERTY FOLDER "${PROJECT_NAME}/test")
endif()

if(NOT TARGET atf4g-co-rpc-unit-test-private-frame)
  add_library(
    atf4g-co-rpc-unit-test-private-frame OBJECT "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}/frame/test_case_base.cpp"
                                                 "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}/frame/test_manager.cpp")
  target_include_directories(atf4g-co-rpc-unit-test-private-frame PRIVATE "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}")
  target_link_libraries(atf4g-co-rpc-unit-test-private-frame PRIVATE ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})
  target_compile_options(atf4g-co-rpc-unit-test-private-frame PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  set_property(TARGET atf4g-co-rpc-unit-test-private-frame PROPERTY FOLDER "${PROJECT_NAME}/test")
endif()

# project_add_rpc_unit_test(
#   TARGET <target>                # required, executable target name
#   COMPONENT <component-name>     # required, used for stable labels/folder, never guessed from paths
#   SOURCES <a.cpp> [<b.cpp> ...]  # required, test sources of this component
#   LINK_LIBRARIES <targets...>    # optional, component SDK/protocol/implementation targets
#   FEATURES <SS DNS CS DB UUID RESOURCE ROUTER ORBIT HPA...> # optional, validated feature tags
#   LABELS <extra ctest labels...>
#   TIMEOUT <seconds>              # ctest process-level timeout, must cover the worst serial-case sum
#   CONFIG <file>                  # optional extra config file copied next to the executable working directory
#   WORKING_DIRECTORY <dir>        # optional, defaults to a per-target directory inside the caller build tree
#   ENVIRONMENT <VAR=value ...>    # optional extra ctest environment
# )
function(project_add_rpc_unit_test)
  set(PROJECT_RPC_UNIT_TEST_KNOWN_FEATURES SS DNS CS DB UUID RESOURCE ROUTER ORBIT HPA TELEMETRY)
  cmake_parse_arguments(PROJECT_RPC_UNIT_TEST "" "TARGET;COMPONENT;TIMEOUT;CONFIG;WORKING_DIRECTORY"
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
  if(PROJECT_RPC_UNIT_TEST_FEATURES MATCHES "(^|;)ORBIT(;|$)" AND NOT TARGET atf4g-co-rpc-unit-test-orbit)
    message(FATAL_ERROR "project_add_rpc_unit_test: feature ORBIT requires target atf4g-co-rpc-unit-test-orbit, "
                        "which is only available when the Orbit client SDK is built")
  endif()

  add_executable(
    ${PROJECT_RPC_UNIT_TEST_TARGET}
    ${PROJECT_RPC_UNIT_TEST_SOURCES} $<TARGET_OBJECTS:atf4g-co-rpc-unit-test-private-main>
    $<TARGET_OBJECTS:atf4g-co-rpc-unit-test-private-frame>)
  target_include_directories(${PROJECT_RPC_UNIT_TEST_TARGET} PRIVATE "${PROJECT_RPC_UNIT_TEST_FRAME_DIR}")
  target_link_libraries(
    ${PROJECT_RPC_UNIT_TEST_TARGET} PRIVATE atf4g-co::rpc-unit-test ${PROJECT_SERVER_FRAME_LIB_LINK}
                                            ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}
                                            ${PROJECT_RPC_UNIT_TEST_LINK_LIBRARIES})
  if(PROJECT_RPC_UNIT_TEST_FEATURES MATCHES "(^|;)ORBIT(;|$)")
    target_link_libraries(${PROJECT_RPC_UNIT_TEST_TARGET} PRIVATE atf4g-co::rpc-unit-test-orbit)
  endif()
  target_compile_options(${PROJECT_RPC_UNIT_TEST_TARGET} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})

  set_target_properties(
    ${PROJECT_RPC_UNIT_TEST_TARGET}
    PROPERTIES INSTALL_RPATH_USE_LINK_PATH YES
               BUILD_WITH_INSTALL_RPATH NO
               BUILD_RPATH_USE_ORIGIN YES)
  set_property(TARGET ${PROJECT_RPC_UNIT_TEST_TARGET} PROPERTY FOLDER "${PROJECT_NAME}/test")

  project_setup_runtime_post_build_bash(${PROJECT_RPC_UNIT_TEST_TARGET} PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_BASH)
  project_setup_runtime_post_build_pwsh(${PROJECT_RPC_UNIT_TEST_TARGET} PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_PWSH)

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

  set(PROJECT_RPC_UNIT_TEST_LABELS atf4g-co unit rpc-unit-test "component:${PROJECT_RPC_UNIT_TEST_COMPONENT}")
  foreach(PROJECT_RPC_UNIT_TEST_FEATURE IN LISTS PROJECT_RPC_UNIT_TEST_FEATURES)
    string(TOLOWER "${PROJECT_RPC_UNIT_TEST_FEATURE}" PROJECT_RPC_UNIT_TEST_FEATURE_LOWER)
    list(APPEND PROJECT_RPC_UNIT_TEST_LABELS "feature:${PROJECT_RPC_UNIT_TEST_FEATURE_LOWER}")
  endforeach()
  list(APPEND PROJECT_RPC_UNIT_TEST_LABELS ${PROJECT_RPC_UNIT_TEST_LABELS})

  if(NOT PROJECT_RPC_UNIT_TEST_TIMEOUT)
    set(PROJECT_RPC_UNIT_TEST_TIMEOUT 120)
  endif()

  set_tests_properties(
    ${PROJECT_RPC_UNIT_TEST_TARGET}.unit
    PROPERTIES LABELS "${PROJECT_RPC_UNIT_TEST_LABELS}"
               TIMEOUT "${PROJECT_RPC_UNIT_TEST_TIMEOUT}"
               WORKING_DIRECTORY "${PROJECT_RPC_UNIT_TEST_WORKING_DIRECTORY}")

  # Windows: executables and dependency DLLs are placed into the unified runtime output directory. CTest uses
  # ENVIRONMENT_MODIFICATION (CMake 3.24+) to prepend it into PATH instead of copying DLLs.
  if(WIN32)
    set_property(
      TEST ${PROJECT_RPC_UNIT_TEST_TARGET}.unit APPEND
      PROPERTY ENVIRONMENT_MODIFICATION
               "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY};PATH=path_list_prepend:${PROJECT_THIRD_PARTY_INSTALL_DIR}/bin"
    )
  endif()

  if(PROJECT_RPC_UNIT_TEST_ENVIRONMENT)
    set_property(TEST ${PROJECT_RPC_UNIT_TEST_TARGET}.unit APPEND PROPERTY ENVIRONMENT
                                                                           "${PROJECT_RPC_UNIT_TEST_ENVIRONMENT}")
  endif()
endfunction()
