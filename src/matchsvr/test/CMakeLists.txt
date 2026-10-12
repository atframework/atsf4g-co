# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =========== Matchsvr Unit Tests ===========
set(MATCHSVR_TEST_TARGET "${PROJECT_NAME}-matchsvr-unit-test")

# Case sources link service::matchsvr::private; the service include directory and SDK/config dependencies propagate from
# it. protocol::matchsvr-protocol is used by the case sources but not linked by the service itself, so it stays
# explicit.
project_add_rpc_unit_test(
  TARGET
  ${MATCHSVR_TEST_TARGET}
  COMPONENT
  matchsvr
  CATEGORY
  service
  SOURCES
  matching_room_test.cpp
  matching_logic_test.cpp
  matching_waiting_rule_test.cpp
  LINK_LIBRARIES
  service::matchsvr::private
  protocol::matchsvr-protocol
  FEATURES
  RESOURCE
  SS
  WORKING_DIRECTORY
  "${CMAKE_CURRENT_BINARY_DIR}"
  LABELS
  fast
  TIMEOUT
  300)
