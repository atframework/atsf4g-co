# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "trade-sdk-search"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  TRADE_SDK_SEARCH_API
  USE_SERVICE_PROTOCOL
  "trade-protocol-search-pbdesc"
  USE_SERVICE_SDK
  "trade-common-sdk"
  HEADERS
  "rpc/trade_search/tradesearchservice.atfw.gen.h"
  "rpc/trade/search.h"
  "trade/search_algorithm.h"
  SOURCES
  "rpc/trade_search/tradesearchservice.atfw.gen.cpp"
  "rpc/trade/search.cpp"
  "trade/search_algorithm.cpp"
  GENERATED_FLOW_NAMES
  "atframework.trade.TradeSearchService")
