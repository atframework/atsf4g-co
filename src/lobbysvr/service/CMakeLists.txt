# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

get_filename_component(SERVICE_DIRECTORY_NAME ${CMAKE_CURRENT_LIST_DIR} DIRECTORY)
get_filename_component(SERVICE_TARGET_NAME ${SERVICE_DIRECTORY_NAME} NAME_WE)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.MatchsvrNotifyService"
  "${CMAKE_CURRENT_LIST_DIR}"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  NO_RPC
  EXTERNAL_SERVICE_PROTOCOLS
  "matchsvr-protocol"
  INCLUDE_HEADERS
  "rpc/matching/matchsvrnotifyservice.atfw.gen.h"
  GENERATED_FLOW_NAME
  "${PROJECT_NAMESPACE}.MatchsvrNotifyService.lobbysvr")

generate_for_pb_add_ss_service(
  "atframework.dtmq.DtmqProxysvrNotifyService"
  "${CMAKE_CURRENT_LIST_DIR}"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  NO_RPC
  EXTERNAL_COMPONENT_PROTOCOLS
  "dtmq-protocol-proxy-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/dtmq_proxy.pb.h"
  GENERATED_FLOW_NAME
  "atframework.dtmq.DtmqProxysvrNotifyService.lobbysvr")

project_service_declare_instance(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  PRECOMPILE_HEADERS
  "data/user.h"
  "data/user_type_define.h"
  "dispatcher/task_action_cs_req_base.h"
  "rpc/db/hash_table.h"
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_COMPONENTS
  "rank-logic-sdk"
  "dtmq-proxy-sdk"
  "ItemAlgorithmSDK"
  "BattleUtilitySDK"
  USE_SERVICE_SDK
  "lobbysvr-sdk"
  "cachesvr-sdk"
  "orbitsvr-sdk"
  "matchsvr-sdk"
  "team-common-sdk"
  "team-sdk-room"
  USE_SERVICE_PROTOCOL
  "lobbysvr-config"
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.MatchsvrNotifyService.lobbysvr"
  "atframework.dtmq.DtmqProxysvrNotifyService.lobbysvr"
  "${PROJECT_NAMESPACE}.LobbysvrClientService"
  "${PROJECT_NAMESPACE}.LobbysvrService")
