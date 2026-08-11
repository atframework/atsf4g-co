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
  ${PROJECT_NAME}-component-dtmq-subscriber-unit-test
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
