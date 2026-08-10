# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol("cachesvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/cache_service.proto")

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.CachesvrService"
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
  CACHE_SERVICE_API
  RPC_DLLEXPORT_DECL
  CACHE_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS
  "cachesvr-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/cache_service.pb.h")

add_subdirectory(sdk)
add_subdirectory(service)