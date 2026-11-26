# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol("trade-protocol-common-common" "${CMAKE_CURRENT_LIST_DIR}/protocol/common" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/common/protocol/common/svr.trade.common.proto")

project_service_declare_protocol("trade-protocol-common-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/common" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/common/protocol/config/trade_common.config.proto")

project_service_declare_protocol(
  "trade-protocol-index-pbdesc" "${CMAKE_CURRENT_LIST_DIR}/protocol/index" USE_COMPONENTS
  "trade-protocol-common-common" PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/index/protocol/pbdesc/trade_index_service.proto")

project_service_declare_protocol(
  "trade-protocol-index-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/index" USE_COMPONENTS
  "trade-protocol-common-config" PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/index/protocol/config/trade_index.config.proto")

generate_for_pb_add_ss_service(
  "atframework.trade.TradeIndexService"
  "${CMAKE_CURRENT_LIST_DIR}/service/index"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  RPC_DLLEXPORT_DECL
  CACHE_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS
  "trade-protocol-index-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/trade_index_service.pb.h")

# add_subdirectory(sdk)
add_subdirectory(service)
