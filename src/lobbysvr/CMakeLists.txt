# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol("lobbysvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/lobby_service.proto")
project_service_declare_protocol("lobbysvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/lobbysvr_config.proto")

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.LobbysvrService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  SERVICE_DLLEXPORT_DECL
  LOBBY_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAME_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS
  "lobbysvr-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/lobby_service.pb.h")

generate_for_pb_add_cs_service(
  "${PROJECT_NAMESPACE}.LobbysvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  RPC_IGNORE_EMPTY_REQUEST
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  GAMECLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAMECLIENT_RPC_API)

add_subdirectory(sdk)
add_subdirectory(service)

if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
