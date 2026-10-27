# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_component_declare_service(
  "compliance-svr"
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "compliance-svr/bin"
  MAIN_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/app/compliance_svr_main.cpp"
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/logic/text_filter_manager.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/logic/text_filter_manager.cpp"
  RESOURCE_DIRECTORIES
  "compliance-svr/log"
  RESOURCE_FILES
  "resource/compliance/text_filter.acv2"
  USE_COMPONENTS
  "compliance-sdk"
  "compliance-svr-config"
  GENERATED_FLOW_NAMES
  "atframework.compliance.ComplianceService")
