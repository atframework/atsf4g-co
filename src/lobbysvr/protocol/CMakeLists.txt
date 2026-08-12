# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol("lobbysvr-protocol" "${CMAKE_CURRENT_LIST_DIR}" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/pbdesc/lobby_service.proto")
project_service_declare_protocol("lobbysvr-config" "${CMAKE_CURRENT_LIST_DIR}" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/config/lobbysvr_config.proto")

get_filename_component(LOBBYSVR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.LobbysvrService"
  "${LOBBYSVR_ROOT_DIR}/service"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${LOBBYSVR_ROOT_DIR}/sdk"
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
  "${LOBBYSVR_ROOT_DIR}/service"
  RPC_IGNORE_EMPTY_REQUEST
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  SERVICE_DLLEXPORT_DECL
  GAMECLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAMECLIENT_RPC_API)
