# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# =========== RPC unit test self tests ===========

# Test-only protocol: instantiates generation branches (router_rpc, user_rpc, non-stream allow_no_wait)
# that have no enabled instance in the production protocols. All generated outputs land in the build tree.
project_service_declare_protocol(
  "rpc-unit-test-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol"
  DLLEXPORT_DECL RPC_UNIT_TEST_PROTOCOL_API
  PROTOCOLS "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/rpc_unit_test.proto")

set(PROJECT_RPC_UNIT_TEST_GENERATED_ROOT "${CMAKE_CURRENT_BINARY_DIR}/generated")
generate_for_pb_add_ss_service(
  "rpc_unit_test.RpcUnitTestService"
  "${CMAKE_CURRENT_BINARY_DIR}/generated-service"
  NO_SERVICE_TASK
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  RPC_ROOT_DIR
  "${PROJECT_RPC_UNIT_TEST_GENERATED_ROOT}"
  SERVICE_DLLEXPORT_DECL
  RPC_UNIT_TEST_SERVICE_API
  RPC_DLLEXPORT_DECL
  RPC_UNIT_TEST_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS
  "rpc-unit-test-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/rpc_unit_test.pb.h")

generate_for_pb_collect_output_from_flows("rpc_unit_test.RpcUnitTestService"
                                          PROJECT_RPC_UNIT_TEST_GENERATED_OUTPUT_FILES)
generate_for_pb_collect_filtered_output_files(
  "${PROJECT_RPC_UNIT_TEST_GENERATED_ROOT}" "${PROJECT_RPC_UNIT_TEST_GENERATED_OUTPUT_FILES}"
  PROJECT_RPC_UNIT_TEST_GENERATED_SOURCES PROJECT_RPC_UNIT_TEST_GENERATED_HEADERS)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(PROJECT_RPC_UNIT_TEST_PROTOCOL_TARGET "pp-rpc-unit-test-protocol")
else()
  set(PROJECT_RPC_UNIT_TEST_PROTOCOL_TARGET "${PROJECT_NAME}-protocol-rpc-unit-test-protocol")
endif()

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-rpc-unit-test-selftest
  COMPONENT rpc-unit-test
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/rpc_unit_test_smoke.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/rpc_unit_test_transport.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/rpc_unit_test_ss.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/rpc_unit_test_router.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/rpc_unit_test_dns.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/rpc_unit_test_db.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/rpc_unit_test_cs.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/example_readme.cpp"
          ${PROJECT_RPC_UNIT_TEST_GENERATED_SOURCES}
          ${PROJECT_RPC_UNIT_TEST_GENERATED_HEADERS}
  LINK_LIBRARIES ${PROJECT_RPC_UNIT_TEST_PROTOCOL_TARGET}
  FEATURES SS DNS DB UUID ROUTER
  LABELS fast
  TIMEOUT 120)

add_dependencies(${PROJECT_NAME}-rpc-unit-test-selftest "generate-for-pb-rpc_unit_test.RpcUnitTestService")
target_include_directories(${PROJECT_NAME}-rpc-unit-test-selftest PRIVATE "${PROJECT_RPC_UNIT_TEST_GENERATED_ROOT}")
