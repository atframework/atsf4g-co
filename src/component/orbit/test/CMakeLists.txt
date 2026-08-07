# ============ Orbit component unit tests (rpc-unit-test based) ============

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-component-orbit-unit-test
  COMPONENT component-orbit
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/orbit_test_server_to_controller_contract.cpp"
  LINK_LIBRARIES components::orbit-server-controller-sdk
  FEATURES SS
  LABELS fast
  TIMEOUT 120)
