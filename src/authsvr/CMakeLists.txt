# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol("authsvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/authsvr_config.proto")

generate_for_pb_add_cs_service(
  "${PROJECT_NAMESPACE}.AuthsvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  RPC_IGNORE_EMPTY_REQUEST
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  LOGINCLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  LOGINCLIENT_RPC_API)

add_subdirectory(service)
