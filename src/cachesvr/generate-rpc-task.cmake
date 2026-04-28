generate_for_pb_add_proto_path("${CMAKE_CURRENT_LIST_DIR}/protocol")
generate_for_pb_add_proto_file("${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/cache_service.proto")

# add tool to binding distributed transaction rpcs
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.CachesvrService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  SERVICE_DLLEXPORT_DECL
  CACHE_SERVICE_API
  RPC_DLLEXPORT_DECL
  CACHE_RPC_API
  INCLUDE_HEADERS
  "protocol/pbdesc/cache_service.pb.h")
