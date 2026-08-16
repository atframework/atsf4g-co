# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =========== server_frame component unit tests ===========
#
# Cases live next to the component under test (see doc/docs/development/rpc-unit-test.md).
# They exercise server_frame behavior (dispatchers, generated RPC/DB APIs, session_manager, uuid) through
# the rpc-unit-test runtime; the framework's own contract tests stay in src/tools/rpc-unit-test/test.
#
# HPA and telemetry cases get their own executables: the opentelemetry provider chain and the named
# telemetry groups are process-lifetime state that is not rebuilt cleanly across fixtures, so cases with
# conflicting telemetry configurations must not share a process (see the telemetry section of
# doc/docs/development/rpc-unit-test.md).

# server_frame_test_db_script_contract.cpp executes the real db_msg_dispatcher Lua scripts in an
# embedded interpreter (redis.call simulator), so the unit-test target links the project Lua.
if(NOT ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LUA_LINK_NAME)
  message(FATAL_ERROR "server-frame db script contract tests require Lua "
                      "(ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LUA_LINK_NAME is empty)")
endif()

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-server-frame-unit-test
  COMPONENT server-frame
  CATEGORY sdk
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_db_login_auth.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_db_script_contract.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_cs_session.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_resource.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_rpc_lru_cache.cpp"
  LINK_LIBRARIES ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LUA_LINK_NAME}
  FEATURES SS DB CS RESOURCE DNS
  LABELS fast
  TIMEOUT 120)

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-server-frame-hpa-unit-test
  COMPONENT server-frame
  CATEGORY sdk
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_hpa.cpp"
  FEATURES HPA
  LABELS fast
  TIMEOUT 120)

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-server-frame-telemetry-unit-test
  COMPONENT server-frame
  CATEGORY sdk
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_telemetry.cpp"
  FEATURES TELEMETRY
  LABELS fast
  TIMEOUT 120)
