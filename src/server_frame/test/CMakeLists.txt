# =========== server_frame component unit tests ===========
#
# Cases live next to the component under test (see src/tools/rpc-unit-test/IMPLEMENTATION_PLAN.md phase 6).
# They exercise server_frame behavior (dispatchers, generated RPC/DB APIs, session_manager, uuid) through
# the rpc-unit-test runtime; the framework's own contract tests stay in src/tools/rpc-unit-test/test.
#
# HPA and telemetry cases get their own executables: the opentelemetry provider chain and the named
# telemetry groups are process-lifetime state that is not rebuilt cleanly across fixtures, so cases with
# conflicting telemetry configurations must not share a process (see IMPLEMENTATION_PLAN.md 8.9).

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-server-frame-unit-test
  COMPONENT server-frame
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_db_login_auth.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_cs_session.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_resource.cpp"
  FEATURES SS DB CS RESOURCE
  LABELS fast
  TIMEOUT 120)

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-server-frame-hpa-unit-test
  COMPONENT server-frame
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_hpa.cpp"
  FEATURES HPA
  LABELS fast
  TIMEOUT 120)

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-server-frame-telemetry-unit-test
  COMPONENT server-frame
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/server_frame_test_telemetry.cpp"
  FEATURES TELEMETRY
  LABELS fast
  TIMEOUT 120)
