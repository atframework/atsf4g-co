include("${CMAKE_CURRENT_LIST_DIR}/generate_for_pb_utility.cmake")

# -----------------------------------------------------------------------------
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/common")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/config")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/pbdesc")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/common")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/config")
file(MAKE_DIRECTORY "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/pbdesc")

generate_for_pb_add_proto_path("${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private")
generate_for_pb_add_proto_path("${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public")
generate_for_pb_add_proto_file(
  "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/common/*.proto"
  "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/config/*.proto"
  "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/private/protocol/pbdesc/*.proto"
  "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/pbdesc/*.proto"
  "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/pbdesc/*.proto"
  "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/public/protocol/pbdesc/*.proto")

# Add additional proto files and paths
function(generate_for_pb_create_protocol_sandbox OUTPUT_DIR)
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")
  unset(OUTPUT_FILES)
  foreach(PROTO_FILE ${ARGN})
    get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME)
    file(CREATE_LINK "${PROTO_FILE}" "${OUTPUT_DIR}/${PROTO_NAME}" COPY_ON_ERROR SYMBOLIC)
    list(APPEND OUTPUT_FILES "${OUTPUT_DIR}/${PROTO_NAME}")
  endforeach()

  set(${OUTPUT_VAR}
      ${${OUTPUT_VAR}} ${OUTPUT_FILES}
      PARENT_SCOPE)
endfunction()

generate_for_pb_add_proto_path("${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb")
if(EXISTS "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb")
  file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb")
endif()

if(PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR
   AND EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto")
  generate_for_pb_create_protocol_sandbox(
    "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb/protocol/common/extensions/v3"
    "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto"
    "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader_ue.proto")
  generate_for_pb_add_proto_file(
    "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb/protocol/common/extensions/v3/xresloader.proto"
    "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb/protocol/common/extensions/v3/xresloader_ue.proto")
endif()
if(PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR
   AND EXISTS "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto")
  generate_for_pb_create_protocol_sandbox(
    "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb/protocol/config"
    "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto")
  generate_for_pb_add_proto_file(
    "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb/protocol/config/xrescode_extensions_v3.proto")
endif()

# -----------------------------------------------------------------------------
# Components
include("${CMAKE_CURRENT_LIST_DIR}/component/distributed_transaction/generate-rpc-task.cmake")

# -----------------------------------------------------------------------------
# add tool to binding router rpcs
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.RouterService"
  "${CMAKE_CURRENT_LIST_DIR}/server_frame"
  TASK_PATH_PREFIX
  "router"
  HANDLE_PATH_PREFIX
  "router"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  SERVER_FRAME_API
  RPC_DLLEXPORT_DECL
  SERVER_FRAME_API)

# add tool to binding logic rpcs
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.LogicCommonService"
  "${CMAKE_CURRENT_LIST_DIR}/server_frame"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "logic"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  SERVER_FRAME_API
  RPC_DLLEXPORT_DECL
  SERVER_FRAME_API)

# add tool to binding loginsvr rpcs
generate_for_pb_add_cs_service(
  "${PROJECT_NAMESPACE}.LoginsvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/loginsvr"
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

# add tool to binding gamesvr rpcs
generate_for_pb_add_cs_service(
  "${PROJECT_NAMESPACE}.GamesvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/gamesvr"
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

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.GamesvrService"
  "${CMAKE_CURRENT_LIST_DIR}/gamesvr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  SERVER_FRAME_API
  RPC_DLLEXPORT_DECL
  SERVER_FRAME_API)

# add tool to generate simulator rpcs
generate_for_pb_add_simulator_cs_api(
  "${PROJECT_NAMESPACE}.LoginsvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/tools/simulator"
  RPC_IGNORE_EMPTY_REQUEST
  SERVICE_DLLEXPORT_DECL
  GAMECLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAMECLIENT_RPC_API
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  GAMECLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAMECLIENT_RPC_API)

generate_for_pb_add_simulator_cs_api(
  "${PROJECT_NAMESPACE}.GamesvrClientService"
  "${CMAKE_CURRENT_LIST_DIR}/tools/simulator"
  RPC_IGNORE_EMPTY_REQUEST
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  GAMECLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  GAMECLIENT_RPC_API)

# Run generator finally
generate_for_pb_run_generator()
