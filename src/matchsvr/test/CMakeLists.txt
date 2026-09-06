# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =========== Matchsvr Unit Tests ===========
set(MATCHSVR_TEST_TARGET "${PROJECT_NAME}-matchsvr-unit-test")

# The matchsvr service compiles everything except app/matchsvr_main.cpp into the static library
# service::matchsvr::private (see project_service_declare_instance MAIN_SOURCES). This target compiles only the case
# sources and links that library; the service include directory and SDK/config dependencies propagate from it.
# protocol::matchsvr-protocol is used by the case sources but not linked by the service itself, so it stays explicit.
set(MATCHSVR_TEST_SOURCES matching_room_test.cpp)
set(MATCHSVR_TEST_LINK_LIBRARIES service::matchsvr::private protocol::matchsvr-protocol)
set(MATCHSVR_TEST_COMPILE_DEFINITIONS)

if(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  list(APPEND MATCHSVR_TEST_SOURCES matching_logic_test.cpp)
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
  COMPILE_DEFINITIONS
  ${MATCHSVR_TEST_COMPILE_DEFINITIONS}
  LABELS
  fast)
