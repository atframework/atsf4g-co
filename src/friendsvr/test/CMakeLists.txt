# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ friendsvr-management service unit tests (rpc-unit-test based) ============
# Case sources link service::friendsvr-management::private; the service include directory, SDK/protocol dependencies and
# the generated-flow build order propagate from it. The skeleton drives the placeholder SS action directly via
# atfw::testing::invoke_ss_action<task_action_management_get_all>, so it does not call
# register_handles_for_friendmanagementservice() and compiles no registration TU here.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-friendsvr-management-unit-test
  COMPONENT
  friendsvr-management
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/friendsvr_management_test.cpp"
  LINK_LIBRARIES
  service::friendsvr-management::private
  FEATURES
  SS
  LABELS
  fast
  TIMEOUT
  120)

project_pch_tool_set_precompile_headers(${PROJECT_NAME}-friendsvr-management-unit-test REUSE_FROM_TARGET
                                        friendsvr-management-private)
