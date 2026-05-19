file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_component_declare_sdk(
  "distributed-transaction-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_TARGET_NAME
  PROJECT_COMPONENT_DISTRIBUTED_TRANSACTION_SDK
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  DISTRIBUTED_TRANSACTION_SDK_API
  USE_COMPONENTS
  distributed-transaction-protocol
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  GENERATED_OUTPUT_FILES
  "atframework.distributed_system.DtcoordsvrService")
