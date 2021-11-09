include("${CMAKE_CURRENT_LIST_DIR}/generate_for_pb_utility.cmake")

# add tool to binding router rpcs
generate_for_pb_add_ss_service(
  "hello.RouterService"
  "${CMAKE_CURRENT_LIST_DIR}/server_frame"
  TASK_PATH_PREFIX
  "router"
  HANDLE_PATH_PREFIX
  "router"
  PROJECT_NAMESPACE
  "hello")

# add tool to binding logic rpcs
generate_for_pb_add_ss_service(
  "hello.LogicCommonService"
  "${CMAKE_CURRENT_LIST_DIR}/server_frame"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "logic"
  PROJECT_NAMESPACE
  "hello")

# add tool to binding gamesvr rpcs
generate_for_pb_add_ss_service(
  "hello.GamesvrService"
  "${CMAKE_CURRENT_LIST_DIR}/gamesvr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "hello")

# Run generator finally
generate_for_pb_run_generator()
