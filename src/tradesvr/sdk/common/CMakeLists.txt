# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_service_declare_sdk(
  "trade-common-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  STATIC
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  TRADE_SDK_COMMON_API
  HEADERS
  "trade/trade_algorithm.h"
  SOURCES
  "trade/trade_algorithm.cpp")
