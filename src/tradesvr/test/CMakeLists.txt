# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-trade-sdk-unit-test
  COMPONENT
  trade
  CATEGORY
  sdk
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/trade_test_batch_rpc.cpp"
  LINK_LIBRARIES
  sdk::trade-sdk-order
  sdk::trade-sdk-search
  FEATURES
  SS
  LABELS
  fast
  TIMEOUT
  60)
