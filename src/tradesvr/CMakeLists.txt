# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_protocol("trade-protocol-common-common" "${CMAKE_CURRENT_LIST_DIR}/protocol/common" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/common/protocol/common/svr.trade.common.proto")

project_service_declare_protocol("trade-protocol-common-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/common" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/common/protocol/config/trade_common.config.proto")

set(TRADE_SERVICE_ROLES index matchmaking order price search)
set(TRADE_SERVICE_INDEX TradeIndexService)
set(TRADE_SERVICE_MATCHMAKING TradeMatchMakingService)
set(TRADE_SERVICE_ORDER TradeOrderService)
set(TRADE_SERVICE_PRICE TradePriceService)
set(TRADE_SERVICE_SEARCH TradeSearchService)

foreach(TRADE_ROLE IN LISTS TRADE_SERVICE_ROLES)
  string(TOUPPER "${TRADE_ROLE}" TRADE_ROLE_UPPER)
  set(TRADE_PROTOCOL_ROOT "${CMAKE_CURRENT_LIST_DIR}/protocol/${TRADE_ROLE}")
  set(TRADE_PROTOCOL_DEPENDENCIES)
  if(TRADE_ROLE STREQUAL "index" OR TRADE_ROLE STREQUAL "order")
    list(APPEND TRADE_PROTOCOL_DEPENDENCIES USE_SERVICE_PROTOCOL "trade-protocol-common-common")
  endif()
  if(TRADE_ROLE STREQUAL "order")
    list(APPEND TRADE_PROTOCOL_DEPENDENCIES USE_COMPONENTS "distributed-transaction-protocol")
  endif()

  project_service_declare_protocol(
    "trade-protocol-${TRADE_ROLE}-pbdesc" "${TRADE_PROTOCOL_ROOT}"
    ${TRADE_PROTOCOL_DEPENDENCIES} PROTOCOLS
    "${TRADE_PROTOCOL_ROOT}/protocol/pbdesc/trade_${TRADE_ROLE}_service.proto")
  project_service_declare_protocol(
    "trade-protocol-${TRADE_ROLE}-config" "${TRADE_PROTOCOL_ROOT}" USE_SERVICE_PROTOCOL
    "trade-protocol-common-config" PROTOCOLS
    "${TRADE_PROTOCOL_ROOT}/protocol/config/trade_${TRADE_ROLE}.config.proto")

  generate_for_pb_add_ss_service(
    "atframework.trade.${TRADE_SERVICE_${TRADE_ROLE_UPPER}}"
    "${CMAKE_CURRENT_LIST_DIR}/service/${TRADE_ROLE}"
    TASK_PATH_PREFIX "logic"
    HANDLE_PATH_PREFIX "app"
    PROJECT_NAMESPACE "${PROJECT_NAMESPACE_ID}"
    RPC_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/sdk/${TRADE_ROLE}"
    RPC_DLLEXPORT_DECL "TRADE_SDK_${TRADE_ROLE_UPPER}_API"
    EXTERNAL_SERVICE_PROTOCOLS "trade-protocol-${TRADE_ROLE}-pbdesc"
    INCLUDE_HEADERS "protocol/pbdesc/trade_${TRADE_ROLE}_service.pb.h")
endforeach()

add_subdirectory(sdk)
add_subdirectory(service)
