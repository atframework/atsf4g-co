# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ Distributed transaction component unit tests (rpc-unit-test based) ============
# Split per UNIT_TEST_EXECUTION_PLAN.md section 3: discovery/replication rules, client vtable
# scripts and participator runtime containers must not pollute each other, and the coordinator
# manager is a process-lifetime singleton whose stop() poisons mutable_transaction forever
# (dedicated executable).

# rpc::transaction_api + generated packer/mock surface: discovery, replication R-of-N, merge/pack
# pure functions and the chrono protocol boundary.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-distributed-transaction-api-unit-test
  COMPONENT
  component-distributed-transaction
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_create_contract.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_api.cpp"
  LINK_LIBRARIES
  components::distributed-transaction-sdk
  FEATURES
  SS
  LABELS
  fast
  distributed-transaction
  TIMEOUT
  180)

# transaction_client_handle: vtable callback scripts, random back-off and the ordered event list
# must stay isolated from participator state.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-distributed-transaction-client-unit-test
  COMPONENT
  component-distributed-transaction
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_client.cpp"
  LINK_LIBRARIES
  components::distributed-transaction-sdk
  FEATURES
  SS
  LABELS
  fast
  distributed-transaction
  TIMEOUT
  180)

# transaction_participator_handle + resolve action: running/finished/lock/timer lifecycles.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-distributed-transaction-participator-unit-test
  COMPONENT
  component-distributed-transaction
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_participator.cpp"
  LINK_LIBRARIES
  components::distributed-transaction-sdk
  FEATURES
  SS
  LABELS
  fast
  distributed-transaction
  TIMEOUT
  300)

# Coordinator internals (manager + 7 actions) are only compiled into the service executable, so the
# sources are compiled directly into the test target like component/dtmq/test does.
set(DTCOORDSVR_INTERNAL_TEST_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/transaction_manager.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/action/task_action_create.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/action/task_action_query.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/action/task_action_commit.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/action/task_action_reject.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/action/task_action_commit_participator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/action/task_action_reject_participator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/action/task_action_remove.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/app/handle_ss_rpc_dtcoordsvrservice.atfw.gen.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_dtcoordsvr.cpp")

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-dtcoordsvr-unit-test
  COMPONENT
  component-dtcoordsvr
  CATEGORY
  service
  SOURCES
  ${DTCOORDSVR_INTERNAL_TEST_SOURCES}
  LINK_LIBRARIES
  components::distributed-transaction-sdk
  components::dtcoordsvr-config
  FEATURES
  SS
  DB
  LABELS
  fast
  distributed-transaction
  TIMEOUT
  300)

# The coordinator action sources use relative includes ("logic/transaction_manager.h"); replicate the
# service target's include behavior.
target_include_directories(${PROJECT_NAME}-component-dtcoordsvr-unit-test
                           PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr")

# transaction_manager::stop() poisons mutable_transaction for the whole process lifetime; keep it in
# a dedicated executable so it cannot affect other coordinator cases.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-dtcoordsvr-stop-unit-test
  COMPONENT
  component-dtcoordsvr
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr/logic/transaction_manager.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_dtcoordsvr_stop.cpp"
  LINK_LIBRARIES
  components::distributed-transaction-sdk
  components::dtcoordsvr-config
  FEATURES
  SS
  DB
  LABELS
  fast
  distributed-transaction
  TIMEOUT
  120)

target_include_directories(${PROJECT_NAME}-component-dtcoordsvr-stop-unit-test
                           PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../dtcoordsvr")

# Short-transaction stress case (bounded retry loops drive real coroutine tasks per round). Labelled
# stress so the default fast regression is not slowed down by the long case.
project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-distributed-transaction-stress-unit-test
  COMPONENT
  component-distributed-transaction
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/distributed_transaction_test_stress.cpp"
  LINK_LIBRARIES
  components::distributed-transaction-sdk
  FEATURES
  SS
  LABELS
  stress
  distributed-transaction
  TIMEOUT
  600)
