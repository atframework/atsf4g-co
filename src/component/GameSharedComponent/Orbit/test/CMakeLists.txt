# ============ Orbit client adapter unit tests (rpc-unit-test based) ============

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-orbit-client-adapter-unit-test
  COMPONENT orbit-client
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/orbit_client_test_send_seam.cpp"
  LINK_LIBRARIES ${ORBIT_CLIENT_SDK_TARGET}
  LABELS fast
  TIMEOUT 120)

target_compile_definitions(${PROJECT_NAME}-orbit-client-adapter-unit-test
  PRIVATE GAME_SHARED_COMPONENT_SERVER_MODE=1)
