# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_get_target_root_dir(components::compliance-svr::private COMPLIANCE_SVR_ROOT_DIR)

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-compliance-unit-test
  COMPONENT
  compliance
  CATEGORY
  service
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/compliance_test_service.cpp"
  "${COMPLIANCE_SVR_ROOT_DIR}/app/handle_ss_rpc_complianceservice.atfw.gen.cpp"
  LINK_LIBRARIES
  components::compliance-svr::private
  FEATURES
  SS
  LABELS
  fast
  compliance
  TIMEOUT
  60)

target_compile_definitions(
  ${PROJECT_NAME}-component-compliance-unit-test
  PRIVATE "COMPLIANCE_TEST_DICTIONARY_FILE=\"${CMAKE_CURRENT_LIST_DIR}/data/text_filter_fixture.acv2\"")

project_add_rpc_unit_test(
  TARGET
  ${PROJECT_NAME}-component-compliance-sdk-unit-test
  COMPONENT
  compliance
  CATEGORY
  sdk
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/compliance_test_client_api.cpp"
  LINK_LIBRARIES
  components::compliance-sdk
  FEATURES
  SS
  LABELS
  fast
  compliance
  TIMEOUT
  60)
