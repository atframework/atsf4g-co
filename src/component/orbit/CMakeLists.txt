# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

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

project_component_declare_protocol(
  "orbit-agent-config" "${CMAKE_CURRENT_LIST_DIR}/agent/protocol"
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/agent/protocol/protocol/config/orbit_agent_config.proto")

project_component_declare_protocol(
  "orbit-controller-config" "${CMAKE_CURRENT_LIST_DIR}/controller/protocol"
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/controller/protocol/protocol/config/orbit_controller_config.proto")

generate_for_pb_add_ss_service(
  "atframework.orbit.ControllerToAgentService"
  "${CMAKE_CURRENT_LIST_DIR}/agent"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/agent"
  SERVICE_DLLEXPORT_DECL
  ORBIT_AGENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  ORBIT_AGENT_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "orbit-agent-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/agent_service.pb.h")

generate_for_pb_add_ss_service(
  "atframework.orbit.AgentToControllerService"
  "${CMAKE_CURRENT_LIST_DIR}/controller"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/controller"
  SERVICE_DLLEXPORT_DECL
  ORBIT_CONTROLLER_SERVICE_API
  RPC_DLLEXPORT_DECL
  ORBIT_CONTROLLER_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "orbit-controller-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/controller_service.pb.h")

generate_for_pb_add_ss_service(
  "atframework.orbit.ServerToControllerService"
  "${CMAKE_CURRENT_LIST_DIR}/controller"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/controller"
  SERVICE_DLLEXPORT_DECL
  ORBIT_CONTROLLER_SERVICE_API
  RPC_DLLEXPORT_DECL
  ORBIT_CONTROLLER_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "orbit-server-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/server_service.pb.h")

generate_for_pb_add_ss_service(
  "atframework.orbit.ControllerToServerService"
  "${CMAKE_CURRENT_LIST_DIR}/sdk/server"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "handle"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/server"
  SERVICE_DLLEXPORT_DECL
  ORBIT_SERVER_SERVICE_API
  RPC_DLLEXPORT_DECL
  ORBIT_SERVER_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "orbit-server-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/server_service.pb.h")

generate_for_pb_add_ss_service(
  "atframework.orbit.ClientToAgentService"
  "${CMAKE_CURRENT_LIST_DIR}/agent"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/agent"
  SERVICE_DLLEXPORT_DECL
  ORBIT_AGENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  ORBIT_AGENT_SDK_API
  NO_RPC
  EXTERNAL_COMPONENT_PROTOCOLS
  "orbit-client-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/client_service.pb.h")

generate_for_pb_add_ss_service(
  "atframework.orbit.AgentToClientService"
  "${CMAKE_CURRENT_LIST_DIR}/sdk/client"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "logic"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/client"
  SERVICE_DLLEXPORT_DECL
  ORBIT_CLIENT_SERVICE_API
  RPC_DLLEXPORT_DECL
  ORBIT_CLIENT_SDK_API
  NO_SERVICE_TASK
  EXTERNAL_COMPONENT_PROTOCOLS
  "orbit-client-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/client_service.pb.h")

add_subdirectory(sdk)
add_subdirectory(agent)
add_subdirectory(controller)

if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()