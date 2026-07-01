get_filename_component(SERVICE_TARGET_NAME ${CMAKE_CURRENT_LIST_DIR} NAME_WE)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_component_declare_service(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_COMPONENTS
  "dtmq-protocol-proxy-pbdesc"
  "dtmq-protocol-proxy-config"
  GENERATED_OUTPUT_FILES
  "atframework.dtmq.DtmqProxysvrService"
  "atframework.dtmq.DtmqProxysvrNotifyService")
