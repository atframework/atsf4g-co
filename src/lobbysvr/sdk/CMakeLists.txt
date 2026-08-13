# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "lobbysvr-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  LOBBY_RPC_API
  USE_SERVICE_PROTOCOL
  lobbysvr-protocol
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/rpc/lobby/lobbysvrservice.atfw.gen.h"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/async_jobs/async_jobs.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/rpc/lobby/lobbysvrservice.atfw.gen.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/async_jobs/async_jobs.cpp"
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.LobbysvrService")
