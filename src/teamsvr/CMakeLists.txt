# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol("team-protocol-match-pbdesc" "${CMAKE_CURRENT_LIST_DIR}/protocol/match" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/match/protocol/pbdesc/team_match_service.proto")

project_service_declare_protocol("team-protocol-match-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/match" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/match/protocol/config/team_match.config.proto")

generate_for_pb_add_ss_service(
  "atframework.team.TeamMatchService"
  "${CMAKE_CURRENT_LIST_DIR}/service/match"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/match"
  RPC_DLLEXPORT_DECL
  TEAM_SDK_MATCH_API
  EXTERNAL_SERVICE_PROTOCOLS
  "team-protocol-match-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/team_match_service.pb.h")

project_service_declare_protocol("team-protocol-room-pbdesc" "${CMAKE_CURRENT_LIST_DIR}/protocol/room" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/room/protocol/pbdesc/team_room_service.proto")

project_service_declare_protocol("team-protocol-room-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/room" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/room/protocol/config/team_room.config.proto")

generate_for_pb_add_ss_service(
  "atframework.team.TeamRoomService"
  "${CMAKE_CURRENT_LIST_DIR}/service/room"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/room"
  RPC_DLLEXPORT_DECL
  TEAM_SDK_ROOM_API
  EXTERNAL_SERVICE_PROTOCOLS
  "team-protocol-room-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/team_room_service.pb.h")

add_subdirectory(sdk)

add_subdirectory(service)

if(PROJECT_ENABLE_UNITTEST AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
