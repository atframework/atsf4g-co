# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ Lobbysvr user_chat_manager unit tests (rpc-unit-test based) ============

# Case sources link service::lobbysvr::private; the service include directory, SDK/protocol dependencies and the
# generated-flow build order propagate from it. The cases call
# handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice() directly, and the registration TU
# belongs to the service executable, so this target compiles that one generated file itself.
project_service_get_target_root_dir(service::lobbysvr::private LOBBYSVR_ROOT_DIR)

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-lobbysvr-unit-test
  COMPONENT
  lobbysvr
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_chat_manager.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_matching_manager.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_manager.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_cache.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_admission.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_kick.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_lifecycle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_cs.cpp"
  "${LOBBYSVR_ROOT_DIR}/app/handle_cs_rpc_lobbysvrclientservice.atfw.gen.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_robust.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_team_dirty.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_async_jobs_db_contract.cpp"
  LINK_LIBRARIES
  service::lobbysvr::private
  FEATURES
  SS
  CS
  LABELS
  fast
  TIMEOUT
  180)

# Ensure the excel resource pipeline (dtmq_channel_type.bytes etc.) is built so the runtime excel bindir resolves
# populated tables.
add_dependencies(${PROJECT_NAME}-lobbysvr-unit-test resource-config)
project_pch_tool_set_precompile_headers(${PROJECT_NAME}-lobbysvr-unit-test REUSE_FROM_TARGET lobbysvr-private)
