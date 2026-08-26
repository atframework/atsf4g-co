# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# The send_message contract test only needs the SS engine (its code path tolerates a missing ExcelDtmqChannelType
# config). Keep it in an SS-only target.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-dtmq-unit-test
  COMPONENT
  component-dtmq
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_send_message_contract.cpp"
  LINK_LIBRARIES
  components::dtmq-proxy-sdk
  FEATURES
  SS
  LABELS
  fast
  TIMEOUT
  120)

# The client_subscriber tests need the SS + RESOURCE engines (client_subscriber::create() queries ExcelDtmqChannelType).
# They live in a separate executable so the SS-only contract test above does not also trigger the excel reload path. All
# subscriber cases share one resource version: the excel config manager is a process-lifetime singleton and only the
# first reload loads the tables; later cases see the same version and skip reload (which also avoids a second reload in
# the same process).
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-dtmq-sdk-proxy-unit-test
  COMPONENT
  component-dtmq
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_send_message_contract.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_client_subscriber.cpp"
  LINK_LIBRARIES
  components::dtmq-proxy-sdk
  FEATURES
  SS
  RESOURCE
  LABELS
  fast
  TIMEOUT
  120)

# Server-side internal unit tests for dtmq-proxysvr: mq_channel_manager + mq_channel public interfaces. The
# channel/channel_manager sources are compiled directly into the test executable (they are otherwise only built into the
# dtmq-proxysvr service executable), and linked against components::dtmq-proxy-sdk for the SS client and the transfer
# and subscribe RPCs. The auto-linked server_frame library provides rpc::db::dtmq_channel_record and mock hooks. SS +
# RESOURCE + DB + HPA engines are required: SS for action forwarding and subscribe/transfer mocks, RESOURCE for
# ExcelDtmqChannelType, DB for dtmq_channel_record mocks, HPA for logic_hpa_current_node_is_in_target().
set(DTMQ_PROXYSVR_INTERNAL_TEST_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/data/mq_channel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/data/mq_channel_wal_handle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_destroy_channel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_find_message.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_page_query_message.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_pull.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_reset_lock.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_send_message.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_subscribe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/action/task_action_update.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr/logic/mq_channel_manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_channel_manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_channel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_task_forward.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_task_subscribe.cpp")

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-dtmq-proxysvr-unit-test
  COMPONENT
  component-dtmq
  CATEGORY
  service
  SOURCES
  ${DTMQ_PROXYSVR_INTERNAL_TEST_SOURCES}
  LINK_LIBRARIES
  components::dtmq-proxy-sdk
  components::dtmq-protocol-proxy-config
  FEATURES
  SS
  RESOURCE
  DB
  HPA
  LABELS
  fast
  TIMEOUT
  180)

# The proxysvr sources use relative includes ("logic/mq_channel_manager.h", "data/mq_channel.h"). The service target
# gets these via project_component_declare_service(SERVICE_ROOT_DIR); replicate that here so the test TU resolves the
# same headers.
target_include_directories(${PROJECT_NAME}-component-dtmq-proxysvr-unit-test
                           PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../dtmq-proxysvr")
