# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "trade-sdk-price"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  TRADE_SDK_PRICE_API
  USE_SERVICE_PROTOCOL
  "trade-protocol-price-pbdesc"
  USE_SERVICE_SDK
  "trade-common-sdk"
  HEADERS
  "rpc/trade_price/tradepriceservice.atfw.gen.h"
  SOURCES
  "rpc/trade_price/tradepriceservice.atfw.gen.cpp"
  GENERATED_FLOW_NAMES
  "atframework.trade.TradePriceService")
