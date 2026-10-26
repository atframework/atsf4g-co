# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_component_declare_sdk(
  "compliance-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  COMPLIANCE_SDK_API
  USE_COMPONENTS
  "compliance-protocol"
  GENERATED_FLOW_NAMES
  "atframework.compliance.ComplianceService")
