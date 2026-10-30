# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(LOBBYSVR_SDK_SOURCES
  logic/mail/mail_utils.cpp
  logic/action/task_action_batch_remove_mail_content.cpp
  rpc/mail/global_mail.cpp
  rpc/mail/user_mail.cpp
  rpc/lobby/lobbysvrservice.atfw.gen.cpp
  rpc/async_jobs/async_jobs.cpp
)
set(LOBBYSVR_SDK_HEADERS
  logic/mail/mail_utils.h
  logic/action/task_action_batch_remove_mail_content.h
  rpc/mail/global_mail.h
  rpc/mail/user_mail.h
  rpc/lobby/lobbysvrservice.atfw.gen.h
  rpc/async_jobs/async_jobs.h
)

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
  ${LOBBYSVR_SDK_HEADERS}
  SOURCES
  ${LOBBYSVR_SDK_SOURCES}
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.LobbysvrService")
