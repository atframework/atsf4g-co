# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(SERVICE_TARGET_NAME orbit-agent)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_component_declare_service(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  USE_COMPONENTS
  "orbit-agent-controller-sdk"
  "orbit-agent-client-sdk"
  "orbit-agent-protocol"
  "orbit-agent-config"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  GENERATED_FLOW_NAMES
  "atframework.orbit.ClientToAgentService"
  "atframework.orbit.ControllerToAgentService")
