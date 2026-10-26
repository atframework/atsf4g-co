# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_component_declare_protocol("compliance-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                   "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/compliance_service.proto")

project_component_declare_protocol("compliance-svr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                   "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/compliance_svr_config.proto")

generate_for_pb_add_ss_service(
  "atframework.compliance.ComplianceService"
  "${CMAKE_CURRENT_LIST_DIR}/compliance_svr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  RPC_DLLEXPORT_DECL
  COMPLIANCE_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "compliance-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/compliance_service.pb.h")

add_subdirectory(sdk)
add_subdirectory(compliance_svr)

if(PROJECT_ENABLE_UNITTEST AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
