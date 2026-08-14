# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(SERVICE_TARGET_NAME teamsvr-room)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

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
  SERVICE_DLLEXPORT_DECL
  TEAM_SERVICE_ROOM_API
  NO_RPC
  EXTERNAL_COMPONENT_PROTOCOLS
  "dtmq-protocol-proxy-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/dtmq_proxy.pb.h"
  GENERATED_FLOW_NAME
  "atframework.dtmq.DtmqProxysvrNotifyService.teamsvr-room")

project_service_declare_instance(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_COMPONENTS
  "dtmq-proxy-sdk"
  USE_SERVICE_SDK
  "team-sdk-room"
  "team-common-sdk"
  USE_SERVICE_PROTOCOL
  "team-protocol-room-config"
  GENERATED_FLOW_NAMES
  "atframework.team.TeamRoomService"
  "atframework.dtmq.DtmqProxysvrNotifyService.teamsvr-room")
