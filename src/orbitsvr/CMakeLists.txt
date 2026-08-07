generate_for_pb_add_orbit_service(
  "${PROJECT_NAMESPACE}.OrbitServerRpcService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  SERVICE_DLLEXPORT_DECL
  ORBIT_SERVER_API
  RPC_DLLEXPORT_DECL
  ORBIT_RPC_API
  INCLUDE_HEADERS
  "protocol/pbdesc/com.orbit.protocol.pb.h")

generate_for_pb_add_orbit_client(
  "${PROJECT_NAMESPACE}.OrbitClientRpcService"
  "${CMAKE_CURRENT_LIST_DIR}/service"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/service"
  SERVICE_DLLEXPORT_DECL
  ORBIT_SERVER_API
  RPC_DLLEXPORT_DECL
  ORBIT_RPC_API
  INCLUDE_HEADERS
  "protocol/pbdesc/com.orbit.protocol.pb.h")

project_service_declare_protocol("orbitsvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/orbit_service.proto")

project_service_declare_protocol("orbitsvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/orbitsvr_config.proto")

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.OrbitsvrService"
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
  ORBIT_SERVER_API
  RPC_DLLEXPORT_DECL
  ORBIT_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS
  "orbitsvr-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/orbit_service.pb.h")

add_subdirectory(service)