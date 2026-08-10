# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ Distributed transaction component unit tests (rpc-unit-test based) ============

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-distributed-transaction-unit-test
  COMPONENT
  component-distributed-transaction
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_create_contract.cpp"
  LINK_LIBRARIES
  components::distributed-transaction-sdk
  FEATURES
  SS
  LABELS
  fast
  TIMEOUT
  120)
