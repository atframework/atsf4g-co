# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ teamsvr-room service unit tests (rpc-unit-test based) ============
# See src/teamsvr/TEAM_ROOM_TEST_PLAN.md. One executable per program/library rule: this single target covers
# action/permission/admission/notification, journal/compaction/snapshot/replay/lock transfer, heartbeat/timer
# wheel/offline/empty-room/destroy and the team-id-0 UUID path.

# teamsvr-room and dtmq-proxysvr compile everything except their entry points (team_room_main.cpp is intentionally
# excluded: the fixture drives team_room_manager::tick() and rpc::dtmq::client_subscriber::global_tick() itself)
# into the static libraries service::teamsvr-room::private and components::dtmq-proxysvr::private (see
# project_service_declare_instance/project_component_declare_service MAIN_SOURCES). This target compiles only the
# case sources and links both libraries; the WAL-layer cases (teamsvr_room_test_wal.cpp) reuse the real mq_channel
# objects (writable journal + wal_publisher/wal_object) from components::dtmq-proxysvr::private, mirroring
# component-dtmq-proxysvr-unit-test. Service include directories, SDK/protocol dependencies and generated-flow build
# order (TeamRoomService invoke_ss_action packer metadata) propagate from the libraries.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-teamsvr-room-unit-test
  COMPONENT
  teamsvr
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_action.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_permission.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_admission.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_concurrency.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_event.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_recovery.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_lifecycle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_sdk_api.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/teamsvr_room_test_wal.cpp"
  LINK_LIBRARIES
  service::teamsvr-room::private
  components::dtmq-proxysvr::private
  FEATURES
  SS
  RESOURCE
  DB
  HPA
  LABELS
  fast
  TIMEOUT
  600)

# Ensure the excel resource pipeline (dtmq_channel_type.bytes etc.) is built so the runtime excel bindir
# resolves populated tables.
add_dependencies(${PROJECT_NAME}-teamsvr-room-unit-test resource-config)

project_pch_tool_set_precompile_headers(${PROJECT_NAME}-teamsvr-room-unit-test REUSE_FROM_TARGET
                                        teamsvr-room-private)
