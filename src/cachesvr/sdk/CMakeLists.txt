# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "cachesvr-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  CACHE_RPC_API
  USE_SERVICE_PROTOCOL
  cachesvr-protocol
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/rpc/cache/cache_algorithm.h"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/cache/cache_api.h"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/cache/cachesvrservice.atfw.gen.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/rpc/cache/cache_algorithm.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/cache/cache_api.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/cache/cachesvrservice.atfw.gen.cpp"
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.CachesvrService")
