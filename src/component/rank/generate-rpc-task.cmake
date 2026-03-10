generate_for_pb_add_proto_path(
  "${CMAKE_CURRENT_LIST_DIR}/protocol")

generate_for_pb_add_proto_file(
  "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/rank_service.proto")

# add tool to binding distributed transaction rpcs
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.RanksvrService"
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/ranksvr"
  SERVICE_DLLEXPORT_DECL
  RANK_SERVICE_API
  RPC_DLLEXPORT_DECL
  RANK_SDK_API
  INCLUDE_HEADERS
  "protocol/pbdesc/rank_service.pb.h")