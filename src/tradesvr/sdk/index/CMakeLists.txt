# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "trade-sdk-index"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  TRADE_SDK_INDEX_API
  USE_SERVICE_PROTOCOL
  "trade-protocol-index-pbdesc"
  USE_SERVICE_SDK
  "trade-common-sdk"
  HEADERS
  "rpc/trade_index/tradeindexservice.atfw.gen.h"
  "rpc/trade/index_algorithm.h"
  "rpc/trade/indexing.h"
  SOURCES
  "rpc/trade_index/tradeindexservice.atfw.gen.cpp"
  "rpc/trade/index_algorithm.cpp"
  "rpc/trade/indexing.cpp"
  GENERATED_FLOW_NAMES
  "atframework.trade.TradeIndexService")
