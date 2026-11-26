# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "trade-sdk-order"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  TRADE_SDK_ORDER_API
  USE_SERVICE_PROTOCOL
  "trade-protocol-order-pbdesc"
  USE_SERVICE_SDK
  "trade-common-sdk"
  HEADERS
  "rpc/trade_order/tradeorderservice.atfw.gen.h"
  SOURCES
  "rpc/trade_order/tradeorderservice.atfw.gen.cpp"
  GENERATED_FLOW_NAMES
  "atframework.trade.TradeOrderService")
