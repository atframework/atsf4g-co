# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol(
  "friend-protocol-management-pbdesc" "${CMAKE_CURRENT_LIST_DIR}/protocol/management" USE_COMPONENTS
  "distributed-transaction-protocol" PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/management/protocol/pbdesc/friend_management_service.proto")

project_service_declare_protocol(
  "friend-protocol-management-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/management" PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/management/protocol/config/friend_management.config.proto")

generate_for_pb_add_ss_service(
  "atframework.friend_api.FriendManagementService"
  "${CMAKE_CURRENT_LIST_DIR}/service/management"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/management"
  RPC_DLLEXPORT_DECL
  FRIEND_SDK_MANAGEMENT_API
  EXTERNAL_SERVICE_PROTOCOLS
  "friend-protocol-management-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/friend_management_service.pb.h"
  GENERATED_FLOW_NAME
  "atframework.friend_api.FriendManagementService")

project_service_declare_protocol(
  "friend-protocol-recommend-pbdesc" "${CMAKE_CURRENT_LIST_DIR}/protocol/recommend" PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/recommend/protocol/pbdesc/friend_recommend_service.proto")

project_service_declare_protocol(
  "friend-protocol-recommend-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/recommend" PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/recommend/protocol/config/friend_recommend.config.proto")

generate_for_pb_add_ss_service(
  "atframework.friend_api.FriendRecommendService"
  "${CMAKE_CURRENT_LIST_DIR}/service/recommend"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/recommend"
  RPC_DLLEXPORT_DECL
  FRIEND_SDK_RECOMMEND_API
  EXTERNAL_SERVICE_PROTOCOLS
  "friend-protocol-recommend-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/friend_recommend_service.pb.h"
  GENERATED_FLOW_NAME
  "atframework.friend_api.FriendRecommendService")

add_subdirectory(sdk)

add_subdirectory(service)

if(PROJECT_ENABLE_UNITTEST AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
