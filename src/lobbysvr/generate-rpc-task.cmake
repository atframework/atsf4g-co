generate_for_pb_add_proto_path("${CMAKE_CURRENT_LIST_DIR}/protocol")
generate_for_pb_add_proto_file("${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/lobby_service.proto")

# add tool to binding distributed transaction rpcs
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.LobbysvrService"
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
  GAME_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAME_RPC_API
  INCLUDE_HEADERS
  "protocol/pbdesc/lobby_service.pb.h")

# add tool to binding lobbysvr rpcs
generate_for_pb_add_cs_service(
  "${PROJECT_NAMESPACE}.LobbysvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  RPC_IGNORE_EMPTY_REQUEST
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  GAMECLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAMECLIENT_RPC_API)
