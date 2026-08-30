# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ teamsvr-room service unit tests (rpc-unit-test based) ============
# See src/teamsvr/TEAM_ROOM_TEST_PLAN.md. One executable per program/library rule: this single target covers
# action/permission/admission/notification, journal/compaction/snapshot/replay/lock transfer, heartbeat/timer
# wheel/offline/empty-room/destroy and the team-id-0 UUID path.

# team_room, the room manager, the room task actions and the DTMQ event-sync action live in the
# teamsvr-room service executable (no library target), so this target compiles the required service
# sources directly. team_room_main.cpp is intentionally NOT compiled: the fixture drives
# team_room_manager::tick() and rpc::dtmq::client_subscriber::global_tick() itself.
set(TEAMSVR_ROOM_TEST_SERVICE_DIR "${CMAKE_CURRENT_LIST_DIR}/../service/room")

# WAL-layer cases (teamsvr_room_test_wal.cpp) additionally compile the real mq_channel sources
# (writable journal + wal_publisher/wal_object) in-process, mirroring
# component-dtmq-proxysvr-unit-test. The fixture bypasses only the distribution/forwarding layer.
set(DTMQ_PROXYSVR_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../component/dtmq/dtmq-proxysvr")

# Collect generated files for the TeamRoomService flow so invoke_ss_action has the packer metadata and
# the dependency ordering is correct (same mechanism as project_service_declare_instance). The flow name
# matches the one registered in src/teamsvr/CMakeLists.txt (proto-package qualified, not ${PROJECT_NAMESPACE}).
generate_for_pb_collect_output_from_flows("atframework.team.TeamRoomService"
                                          _TEAMSVR_ROOM_TEST_GENERATED_OUTPUT_FILES)
generate_for_pb_collect_filtered_output_files(
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}" "${_TEAMSVR_ROOM_TEST_GENERATED_OUTPUT_FILES}"
  _TEAMSVR_ROOM_TEST_GENERATED_SOURCE_FILES _TEAMSVR_ROOM_TEST_GENERATED_HEADER_FILES)

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
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/room/team_room.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/room/team_room_manager.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_add_invitation.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_add_join_request.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_approve_invitation.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_approve_join_request.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_create.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_heartbeat.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_reject_invitation.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_reject_join_request.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/action/task_action_send_message.cpp"
  "${TEAMSVR_ROOM_TEST_SERVICE_DIR}/logic/dtmq/task_action_channel_event_sync.cpp"
  "${DTMQ_PROXYSVR_DATA_DIR}/data/mq_channel.cpp"
  "${DTMQ_PROXYSVR_DATA_DIR}/data/mq_channel_wal_handle.cpp"
  "${DTMQ_PROXYSVR_DATA_DIR}/logic/mq_channel_manager.cpp"
  ${_TEAMSVR_ROOM_TEST_GENERATED_SOURCE_FILES}
  LINK_LIBRARIES
  components::dtmq-proxy-sdk
  components::dtmq-protocol-proxy-config
  sdk::team-sdk-room
  sdk::team-common-sdk
  protocol::team-protocol-room-config
  FEATURES
  SS
  RESOURCE
  DB
  HPA
  LABELS
  fast
  TIMEOUT
  600)

# Generation must complete before compilation of the test target (generated *.atfw.gen.* headers are
# consumed by the handwritten sources). Same mechanism as project_service_declare_instance.
generate_for_pb_add_dependencies(${PROJECT_NAME}-teamsvr-room-unit-test "atframework.team.TeamRoomService")

# Ensure the excel resource pipeline (dtmq_channel_type.bytes etc.) is built so the runtime excel bindir
# resolves populated tables.
add_dependencies(${PROJECT_NAME}-teamsvr-room-unit-test resource-config)

target_include_directories(${PROJECT_NAME}-teamsvr-room-unit-test
                           PRIVATE "$<BUILD_INTERFACE:${TEAMSVR_ROOM_TEST_SERVICE_DIR}>"
                                   "$<BUILD_INTERFACE:${DTMQ_PROXYSVR_DATA_DIR}>")

project_pch_tool_set_precompile_headers(${PROJECT_NAME}-teamsvr-room-unit-test REUSE_FROM_TARGET teamsvr-room)
