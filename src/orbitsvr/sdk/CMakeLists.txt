# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "orbitsvr-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  ORBIT_RPC_API
  USE_SERVICE_PROTOCOL
  orbitsvr-protocol
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/rpc/orbit/orbitsvrservice.atfw.gen.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/rpc/orbit/orbitsvrservice.atfw.gen.cpp"
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.OrbitsvrService")
