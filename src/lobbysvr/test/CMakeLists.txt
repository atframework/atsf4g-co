# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ Lobbysvr user_chat_manager unit tests (rpc-unit-test based) ============

# user_chat_manager, the chat task actions and their supporting user/manager code live in the lobbysvr service
# executable (no library target), so this target compiles the required service sources directly. The generated
# *.atfw.gen.* sources for the CS flow are collected from the generation target (same mechanism as
# project_service_declare_instance) so the dependency ordering is correct and we never edit generated files by hand.
set(LOBBYSVR_CHAT_TEST_SERVICE_DIR "${CMAKE_CURRENT_LIST_DIR}/../service")

# Collect generated files for the CS flow (handle_cs_rpc + rpc/lobbysvrclientservice).
generate_for_pb_collect_output_from_flows("${PROJECT_NAMESPACE}.LobbysvrClientService"
                                          _LOBBYSVR_CHAT_TEST_GENERATED_OUTPUT_FILES)
generate_for_pb_collect_filtered_output_files(
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}" "${_LOBBYSVR_CHAT_TEST_GENERATED_OUTPUT_FILES}"
  _LOBBYSVR_CHAT_TEST_GENERATED_SOURCE_FILES _LOBBYSVR_CHAT_TEST_GENERATED_HEADER_FILES)

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-lobbysvr-unit-test
  COMPONENT
  lobbysvr
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_user_chat_manager.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_async_jobs_db_contract.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/data/user.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/action/task_action_access_update.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/action/task_action_login.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/action/task_action_ping.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/action/task_action_user_async_jobs.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/async_jobs/task_action_user_remote_patch_jobs.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/async_jobs/user_async_jobs_manager.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/cache/global_cache_manager.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/cache/task_action_cache_api_batch_get.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/cache/task_action_cache_api_notify_meta.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/cache/task_action_cache_api_unwatch.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/cache/user_cache_manager.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/chat/task_action_chat_channel_heartbeat.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/chat/task_action_chat_get_all_channel.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/chat/task_action_chat_get_channel_snapshot.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/chat/task_action_chat_send_message.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/chat/user_chat_manager.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/matching/user_matching_manager.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/user/task_action_user_get_info.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/rank/task_action_rank_get_self_rank.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/rank/task_action_rank_get_specify_rank.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/rank/user_rank_manager.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/orbit/user_orbit_manager.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/user/task_action_user_gm_cmd_nomsg.cpp"
  "${LOBBYSVR_CHAT_TEST_SERVICE_DIR}/logic/user/task_action_user_gm_command.cpp"
  ${_LOBBYSVR_CHAT_TEST_GENERATED_SOURCE_FILES}
  LINK_LIBRARIES
  sdk::lobbysvr-sdk
  sdk::cachesvr-sdk
  sdk::matchsvr-sdk
  sdk::orbitsvr-sdk
  components::dtmq-proxy-sdk
  components::rank-logic-sdk
  protocol::lobbysvr-config
  FEATURES
  SS
  CS
  LABELS
  fast
  TIMEOUT
  180)

# Generation must complete before compilation of the test target (generated *.atfw.gen.* headers are consumed by the
# handwritten sources). Same mechanism as project_service_declare_instance.
generate_for_pb_add_dependencies(${PROJECT_NAME}-lobbysvr-unit-test "${PROJECT_NAMESPACE}.LobbysvrClientService")

# Ensure the excel resource pipeline (dtmq_channel_type.bytes etc.) is built so the runtime excel bindir resolves
# populated tables.
add_dependencies(${PROJECT_NAME}-lobbysvr-unit-test resource-config)

target_include_directories(${PROJECT_NAME}-lobbysvr-unit-test
                           PRIVATE "$<BUILD_INTERFACE:${LOBBYSVR_CHAT_TEST_SERVICE_DIR}>")

project_pch_tool_set_precompile_headers(${PROJECT_NAME}-lobbysvr-unit-test REUSE_FROM_TARGET lobbysvr)
