generate_for_pb_add_proto_path("${CMAKE_CURRENT_LIST_DIR}/protocol")
generate_for_pb_add_proto_file("${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/distributed_transaction.proto")

# add tool to binding distributed transaction rpcs
generate_for_pb_add_ss_service(
  "atframework.distributed_system.DtcoordsvrService"
  "${CMAKE_CURRENT_LIST_DIR}/dtcoordsvr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk"
  INCLUDE_HEADERS
  "protocol/pbdesc/distributed_transaction.pb.h")
