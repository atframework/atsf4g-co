# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =========== Matchsvr Unit Tests ===========
set(MATCHSVR_TEST_FRAME_DIR "${PROJECT_SOURCE_DIR}/atframework/atframe_utils/test")
set(MATCHSVR_TEST_TARGET "${PROJECT_NAME}-matchsvr-unit-test")

add_executable(
  ${MATCHSVR_TEST_TARGET}
  main.cpp
  matching_room_test.cpp
  "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_room.cpp"
  "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_utility.cpp"
  "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_wal_handle.cpp"
  "${MATCHSVR_TEST_FRAME_DIR}/frame/test_case_base.cpp"
  "${MATCHSVR_TEST_FRAME_DIR}/frame/test_manager.cpp")

if(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  target_sources(
    ${MATCHSVR_TEST_TARGET}
    PRIVATE matching_logic_test.cpp "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_logic.cpp"
            "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_manager.cpp")
endif()

target_include_directories(${MATCHSVR_TEST_TARGET} PRIVATE "${MATCHSVR_TEST_FRAME_DIR}"
                                                           "${PROJECT_SOURCE_DIR}/src/matchsvr/service")
target_link_libraries(${MATCHSVR_TEST_TARGET} PRIVATE server_frame protocol::matchsvr-protocol sdk::matchsvr-sdk
                                                      sdk::orbitsvr-sdk)
if(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  target_link_libraries(${MATCHSVR_TEST_TARGET} PRIVATE ${PROJECT_NAME}::rpc-unit-test)
  target_compile_definitions(${MATCHSVR_TEST_TARGET}
                             PRIVATE MATCHSVR_TEST_WORKING_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
endif()

project_tool_split_target_debug_sybmol(${MATCHSVR_TEST_TARGET})
project_tool_set_target_runtime_output_directory("${PROJECT_INSTALL_BAS_DIR}/bin" ${MATCHSVR_TEST_TARGET}
                                                 WITH_TARGET_RPATH WITH_ARCHIVE_RPATH)

set_property(TARGET ${MATCHSVR_TEST_TARGET} PROPERTY FOLDER "${PROJECT_NAME}/test")
add_test(NAME ${MATCHSVR_TEST_TARGET} COMMAND ${MATCHSVR_TEST_TARGET})
