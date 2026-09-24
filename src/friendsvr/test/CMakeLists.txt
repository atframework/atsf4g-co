# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ friendsvr-management service unit tests (rpc-unit-test based) ============
# Exercise the generated clients and real service actions with offline SS/DB boundaries.
project_service_get_target_root_dir(service::friendsvr-management::private FRIEND_MANAGEMENT_ROOT_DIR)
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-friendsvr-management-unit-test
  COMPONENT
  friendsvr-management
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/friendsvr_management_test.cpp"
  "${FRIEND_MANAGEMENT_ROOT_DIR}/app/handle_ss_rpc_friendmanagementservice.atfw.gen.cpp"
  LINK_LIBRARIES
  service::friendsvr-management::private
  FEATURES
  SS
  DB
  ROUTER
  LABELS
  fast
  TIMEOUT
  120)

project_pch_tool_set_precompile_headers(${PROJECT_NAME}-friendsvr-management-unit-test REUSE_FROM_TARGET
                                        friendsvr-management-private)
