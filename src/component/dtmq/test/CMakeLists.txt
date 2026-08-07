# ============ DTMQ component unit tests (rpc-unit-test based) ============

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-component-dtmq-unit-test
  COMPONENT component-dtmq
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/dtmq_test_send_message_contract.cpp"
  LINK_LIBRARIES components::dtmq-proxy-sdk
  FEATURES SS
  LABELS fast
  TIMEOUT 120)
