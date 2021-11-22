include("${CMAKE_CURRENT_LIST_DIR}/generate_for_pb_utility.cmake")

# add tool to binding router rpcs
generate_for_pb_add_ss_service(
  "${PROJECT_SERVER_FRAME_NAMESPACE}.RouterService"
  "${CMAKE_CURRENT_LIST_DIR}/server_frame"
  TASK_PATH_PREFIX
  "router"
  HANDLE_PATH_PREFIX
  "router"
  PROJECT_NAMESPACE
  "${PROJECT_SERVER_FRAME_NAMESPACE}")

# add tool to binding logic rpcs
generate_for_pb_add_ss_service(
  "${PROJECT_SERVER_FRAME_NAMESPACE}.LogicCommonService"
  "${CMAKE_CURRENT_LIST_DIR}/server_frame"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "logic"
  PROJECT_NAMESPACE
  "${PROJECT_SERVER_FRAME_NAMESPACE}")

# add tool to binding loginsvr rpcs
generate_for_pb_add_cs_service(
  "${PROJECT_SERVER_FRAME_NAMESPACE}.LoginsvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/loginsvr"
  RPC_IGNORE_EMPTY_REQUEST
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_SERVER_FRAME_NAMESPACE}")

# add tool to binding gamesvr rpcs
generate_for_pb_add_cs_service(
  "${PROJECT_SERVER_FRAME_NAMESPACE}.GamesvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/gamesvr"
  RPC_IGNORE_EMPTY_REQUEST
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_SERVER_FRAME_NAMESPACE}")

generate_for_pb_add_ss_service(
  "${PROJECT_SERVER_FRAME_NAMESPACE}.GamesvrService"
  "${CMAKE_CURRENT_LIST_DIR}/gamesvr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_SERVER_FRAME_NAMESPACE}")

# add tool to generate simulator rpcs
generate_for_pb_add_simulator_cs_api(
  "${PROJECT_SERVER_FRAME_NAMESPACE}.LoginsvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/tools/simulator"
  RPC_IGNORE_EMPTY_REQUEST
  PROJECT_NAMESPACE
  "${PROJECT_SERVER_FRAME_NAMESPACE}"
)

generate_for_pb_add_simulator_cs_api(
  "${PROJECT_SERVER_FRAME_NAMESPACE}.GamesvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/tools/simulator"
  RPC_IGNORE_EMPTY_REQUEST
  PROJECT_NAMESPACE
  "${PROJECT_SERVER_FRAME_NAMESPACE}"
)

# Run generator finally
generate_for_pb_run_generator()
