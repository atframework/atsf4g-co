# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

get_filename_component(SERVICE_TARGET_NAME ${CMAKE_CURRENT_LIST_DIR} NAME_WE)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_component_declare_service(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  MAIN_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/logic/action/task_action_send_message.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/app/dtmq_proxy_main.cpp"
  # The executable, not the private library, carries the task_action_* TUs whose global class names collide with
  # other services: task_action_channel_event_sync implements the notify proto service, which teamsvr-room and
  # lobbysvr also implement, and task_action_send_message shares an RPC name with TeamRoomService.send_message
  # (generated task action classes are named after the RPC only). Private libraries holding duplicate task_action_*
  # definitions can never be co-linked (issue #210), so these stay in the executable and
  # components::dtmq-proxysvr::private remains co-linkable with those services' private libraries.
  MAIN_DIRECTORIES
  app
  logic/dtmq
  MAIN_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/logic/action/task_action_send_message.h"
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_COMPONENTS
  "dtmq-protocol-proxy-config"
  "dtmq-proxy-sdk"
  GENERATED_FLOW_NAMES
  "atframework.dtmq.DtmqProxysvrService"
  "atframework.dtmq.DtmqProxysvrNotifyService")

