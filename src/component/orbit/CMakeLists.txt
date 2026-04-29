project_component_declare_protocol(
  "orbit-common-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol/common"
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/common/protocol/common/orbit.common.proto")

project_component_declare_protocol(
  "orbit-agent-protocol" "${CMAKE_CURRENT_LIST_DIR}/agent/protocol"
  USE_COMPONENTS
  "orbit-common-protocol"
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/agent/protocol/protocol/pbdesc/agent_service.proto")

project_component_declare_protocol(
  "orbit-controller-protocol" "${CMAKE_CURRENT_LIST_DIR}/controller/protocol"
  USE_COMPONENTS
  "orbit-common-protocol"
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/controller/protocol/protocol/pbdesc/controller_service.proto")

project_component_declare_protocol(
  "orbit-server-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol/server"
  USE_COMPONENTS
  "orbit-common-protocol"
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/server/protocol/pbdesc/server_service.proto")

project_component_declare_protocol(
  "orbit-client-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol/client"
  USE_COMPONENTS
  "orbit-common-protocol"
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/client/protocol/pbdesc/client_service.proto")

add_subdirectory(sdk)

add_subdirectory(agent)
add_subdirectory(controller)