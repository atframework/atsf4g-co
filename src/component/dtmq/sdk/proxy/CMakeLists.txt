set(SERVICE_SOURCES rpc/dtmq/dtmqproxysvrservice.atfw.gen.cpp rpc/dtmq/dtmqproxysvrnotifyservice.atfw.gen.cpp)
set(SERVICE_HEADERS rpc/dtmq/dtmqproxysvrservice.atfw.gen.h rpc/dtmq/dtmqproxysvrnotifyservice.atfw.gen.h)

project_component_declare_sdk(
  "dtmq-proxy-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  DTMQ_PROXY_SDK_API
  USE_COMPONENTS
  "dtmq-protocol-proxy-pbdesc"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  GENERATED_OUTPUT_FILES
  "atframework.dtmq.DtmqProxysvrService"
  "atframework.dtmq.DtmqProxysvrNotifyService")
