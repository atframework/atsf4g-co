# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_component_declare_protocol(
  "dtmq-protocol-common" "${CMAKE_CURRENT_LIST_DIR}/protocol/common" PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/common/protocol/common/svr.struct.dtmq.common.proto")

project_component_declare_protocol(
  "dtmq-protocol-proxy-pbdesc" "${CMAKE_CURRENT_LIST_DIR}/protocol/proxy" USE_COMPONENTS "dtmq-protocol-common"
  PROTOCOLS "${CMAKE_CURRENT_LIST_DIR}/protocol/proxy/protocol/pbdesc/dtmq_proxy.proto")

project_component_declare_protocol(
  "dtmq-protocol-proxy-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/proxy" USE_COMPONENTS "dtmq-protocol-common"
  PROTOCOLS "${CMAKE_CURRENT_LIST_DIR}/protocol/proxy/protocol/config/dtmq_proxy.config.proto")

generate_for_pb_add_ss_service(
  "atframework.dtmq.DtmqProxysvrService"
  "${CMAKE_CURRENT_LIST_DIR}/dtmq-proxysvr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/proxy"
  RPC_DLLEXPORT_DECL
  DTMQ_PROXY_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "dtmq-protocol-proxy-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/dtmq_proxy.pb.h")

generate_for_pb_add_ss_service(
  "atframework.dtmq.DtmqProxysvrNotifyService"
  "${CMAKE_CURRENT_LIST_DIR}/dtmq-proxysvr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/proxy"
  RPC_DLLEXPORT_DECL
  DTMQ_PROXY_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "dtmq-protocol-proxy-pbdesc"
  INCLUDE_HEADERS
  "protocol/pbdesc/dtmq_proxy.pb.h")

add_subdirectory(sdk)

add_subdirectory(dtmq-proxysvr)

if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
