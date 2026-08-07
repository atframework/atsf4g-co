project_component_declare_protocol("distributed-transaction-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                   "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/distributed_transaction.proto")

project_component_declare_protocol("dtcoordsvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                   "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/dtcoordsvr_config.proto")

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
  SERVICE_DLLEXPORT_DECL
  DISTRIBUTED_TRANSACTION_SERVICE_API
  RPC_DLLEXPORT_DECL
  DISTRIBUTED_TRANSACTION_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "distributed-transaction-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/distributed_transaction.pb.h")

add_subdirectory(sdk)
add_subdirectory(dtcoordsvr)

if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
