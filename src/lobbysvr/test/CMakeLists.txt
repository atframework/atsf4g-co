# ============ Lobbysvr component unit tests (rpc-unit-test based) ============

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-lobbysvr-unit-test
  COMPONENT lobbysvr
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/lobbysvr_test_async_jobs_db_contract.cpp"
  LINK_LIBRARIES sdk::lobbysvr-sdk
  FEATURES DB
  LABELS fast
  TIMEOUT 120)
