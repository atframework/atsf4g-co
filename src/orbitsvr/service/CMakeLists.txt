set(SERVICE_TARGET_NAME orbit-server)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_service_declare_instance(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  USE_SERVICE_SDK
  "lobbysvr-sdk"
  USE_COMPONENTS
  "orbit-common-protocol"
  "orbit-server-sdk"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_SERVICE_PROTOCOL
  "orbitsvr-protocol"
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.OrbitServerRpcService"
  "${PROJECT_NAMESPACE}.OrbitClientRpcService"
  "${PROJECT_NAMESPACE}.OrbitsvrService")
