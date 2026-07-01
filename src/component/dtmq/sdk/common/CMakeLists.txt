# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(DTMQ_COMMON_SERVICE_SOURCES rpc/dtmq/dtmq_algorithm.cpp)
set(DTMQ_COMMON_SERVICE_HEADERS rpc/dtmq/dtmq_algorithm.h)

project_component_declare_sdk(
  "dtmq-common-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  DTMQ_COMMON_SDK_API
  USE_COMPONENTS
  "dtmq-protocol-common"
  HEADERS
  ${DTMQ_COMMON_SERVICE_HEADERS}
  SOURCES
  ${DTMQ_COMMON_SERVICE_SOURCES})
