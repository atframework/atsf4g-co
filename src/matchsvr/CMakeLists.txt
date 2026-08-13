project_service_declare_protocol("matchsvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/match_service.proto")

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.MatchsvrService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  RPC_DLLEXPORT_DECL
  MATCH_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS
  "matchsvr-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/match_service.pb.h"
  GENERATED_FLOW_NAME
  "${PROJECT_NAMESPACE}.MatchsvrService")

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.MatchsvrNotifyService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  RPC_DLLEXPORT_DECL
  MATCH_RPC_API
  NO_SERVICE_TASK
  EXTERNAL_SERVICE_PROTOCOLS
  "matchsvr-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/match_service.pb.h"
  GENERATED_FLOW_NAME
  "${PROJECT_NAMESPACE}.MatchsvrNotifyService")

add_subdirectory(sdk)
add_subdirectory(service)
if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
