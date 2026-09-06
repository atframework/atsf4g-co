# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ Lobbysvr user_chat_manager unit tests (rpc-unit-test based) ============

# The lobbysvr service compiles everything except app/lobbysvr_main.cpp into the static library
# service::lobbysvr::private (see project_service_declare_instance MAIN_SOURCES). This target compiles only the case
# sources and links that library; the service include directory, SDK/protocol dependencies and the generated-flow
# build order (including handle_cs_rpc + rpc/lobbysvrclientservice *.atfw.gen.* sources) all propagate from it.
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
