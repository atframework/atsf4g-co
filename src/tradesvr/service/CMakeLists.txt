# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(TRADE_SERVICE_INDEX_SDKS "trade-sdk-index" "trade-sdk-order")
set(TRADE_SERVICE_MATCHMAKING_SDKS "trade-sdk-matchmaking" "trade-sdk-index" "trade-sdk-order")
set(TRADE_SERVICE_ORDER_SDKS "lobbysvr-sdk" "trade-sdk-order")
set(TRADE_SERVICE_PRICE_SDKS "trade-sdk-price" "trade-sdk-index" "trade-sdk-order")
set(TRADE_SERVICE_SEARCH_SDKS "trade-sdk-search" "trade-sdk-index" "trade-sdk-order")

foreach(TRADE_ROLE IN LISTS TRADE_SERVICE_ROLES)
  string(TOUPPER "${TRADE_ROLE}" TRADE_ROLE_UPPER)
  set(TRADE_SERVICE_ROOT "${CMAKE_CURRENT_LIST_DIR}/${TRADE_ROLE}")
  project_service_declare_instance(
    "tradesvr-${TRADE_ROLE}"
    "${TRADE_SERVICE_ROOT}"
    INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    MAIN_SOURCES "${TRADE_SERVICE_ROOT}/app/trade_${TRADE_ROLE}_main.cpp"
    RESOURCE_DIRECTORIES "tradesvr-${TRADE_ROLE}/log"
    USE_SERVICE_SDK ${TRADE_SERVICE_${TRADE_ROLE_UPPER}_SDKS}
    USE_SERVICE_PROTOCOL "trade-protocol-${TRADE_ROLE}-config"
    GENERATED_FLOW_NAMES "atframework.trade.${TRADE_SERVICE_${TRADE_ROLE_UPPER}}")
endforeach()
