get_filename_component(SERVICE_DIRECTORY_NAME ${CMAKE_CURRENT_LIST_DIR} DIRECTORY)
get_filename_component(SERVICE_TARGET_NAME ${SERVICE_DIRECTORY_NAME} NAME_WE)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.OrbitsvrMatchsvrService"
  "${CMAKE_CURRENT_LIST_DIR}"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  SERVICE_DLLEXPORT_DECL
  MATCH_SERVICE_API
  NO_RPC
  EXTERNAL_SERVICE_PROTOCOLS
  "orbitsvr-protocol"
  INCLUDE_HEADERS
  "rpc/orbit/orbitsvrmatchsvrservice.atfw.gen.h"
  GENERATED_FLOW_NAME
  "${PROJECT_NAMESPACE}.OrbitsvrMatchsvrService.matchsvr")

project_service_declare_instance(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  USE_SERVICE_SDK
  "matchsvr-sdk"
  "orbitsvr-sdk"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.MatchsvrService"
  "${PROJECT_NAMESPACE}.MatchsvrNotifyService"
  "${PROJECT_NAMESPACE}.OrbitsvrMatchsvrService.matchsvr")
