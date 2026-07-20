# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(DTMQ_PROXY_SERVICE_SOURCES
    rpc/dtmq/dtmqproxysvrservice.atfw.gen.cpp rpc/dtmq/dtmqproxysvrnotifyservice.atfw.gen.cpp
    rpc/dtmq/dtmq_client_api.cpp rpc/dtmq/dtmq_client_subscriber.cpp)
set(DTMQ_PROXY_SERVICE_HEADERS rpc/dtmq/dtmqproxysvrservice.atfw.gen.h rpc/dtmq/dtmqproxysvrnotifyservice.atfw.gen.h
                               rpc/dtmq/dtmq_client_api.h rpc/dtmq/dtmq_client_subscriber.h)

project_component_declare_sdk(
  "dtmq-proxy-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  DTMQ_PROXY_SDK_API
  USE_COMPONENTS
  "dtmq-protocol-proxy-pbdesc"
  "dtmq-common-sdk"
  HEADERS
  ${DTMQ_PROXY_SERVICE_HEADERS}
  SOURCES
  ${DTMQ_PROXY_SERVICE_SOURCES}
  GENERATED_FLOW_NAMES
  "atframework.dtmq.DtmqProxysvrService"
  "atframework.dtmq.DtmqProxysvrNotifyService")
