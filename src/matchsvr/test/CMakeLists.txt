# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =========== Matchsvr Unit Tests ===========
set(MATCHSVR_TEST_TARGET "${PROJECT_NAME}-matchsvr-unit-test")

set(MATCHSVR_TEST_SOURCES
    matching_room_test.cpp
    "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_room.cpp"
    "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_unit.cpp"
    "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_utility.cpp"
    "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_wal_handle.cpp")
set(MATCHSVR_TEST_LINK_LIBRARIES server_frame protocol::matchsvr-protocol protocol::matchsvr-config sdk::matchsvr-sdk
                                 sdk::orbitsvr-sdk)
set(MATCHSVR_TEST_COMPILE_DEFINITIONS)

if(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  list(APPEND MATCHSVR_TEST_SOURCES matching_logic_test.cpp
       "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_logic.cpp"
       "${PROJECT_SOURCE_DIR}/src/matchsvr/service/logic/matching/matching_manager.cpp")
  list(APPEND MATCHSVR_TEST_LINK_LIBRARIES ${PROJECT_NAME}::rpc-unit-test)
  list(APPEND MATCHSVR_TEST_COMPILE_DEFINITIONS MATCHSVR_TEST_WORKING_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
endif()

project_add_normal_unit_test(
  TARGET
  ${MATCHSVR_TEST_TARGET}
  COMPONENT
  matchsvr
  CATEGORY
  service
  SOURCES
  ${MATCHSVR_TEST_SOURCES}
  LINK_LIBRARIES
  ${MATCHSVR_TEST_LINK_LIBRARIES}
  INCLUDE_DIRECTORIES
  "${PROJECT_SOURCE_DIR}/src/matchsvr/service"
  COMPILE_DEFINITIONS
  ${MATCHSVR_TEST_COMPILE_DEFINITIONS}
  LABELS
  fast)
