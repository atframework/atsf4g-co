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

# Server-side internal unit tests for dtmq-proxysvr: mq_channel_manager + mq_channel public interfaces. Case sources
# link components::dtmq-proxysvr::private; the proxysvr include directory and the dtmq-proxy-sdk /
# dtmq-protocol-proxy-config usage requirements propagate from the library, and the two explicit links stay because
# the case sources include them directly. The auto-linked server_frame library provides rpc::db::dtmq_channel_record
# and mock hooks. SS + RESOURCE + DB + HPA engines are required: SS for action forwarding and subscribe/transfer
# mocks, RESOURCE for ExcelDtmqChannelType, DB for dtmq_channel_record mocks, HPA for
# logic_hpa_current_node_is_in_target(). task_action_send_message.cpp is compiled here directly: it belongs to the
# dtmq-proxysvr executable now (its global class name collides with TeamRoomService.send_message, issue #210) and
# the forwarding cases invoke it by type.
project_service_get_target_root_dir(components::dtmq-proxysvr::private DTMQ_PROXYSVR_ROOT_DIR)
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-dtmq-proxysvr-unit-test
  COMPONENT
  component-dtmq
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_channel_manager.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_channel.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_task_forward.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_task_subscribe.cpp"
  "${DTMQ_PROXYSVR_ROOT_DIR}/logic/action/task_action_send_message.cpp"
  LINK_LIBRARIES
  components::dtmq-proxysvr::private
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
