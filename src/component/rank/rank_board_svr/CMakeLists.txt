# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(SERVICE_TARGET_NAME rank-board-svr)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_component_declare_service(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  USE_COMPONENTS
  "rank-logic-sdk"
  "rank-board-config"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  GENERATED_FLOW_NAMES
  "${PROJECT_NAMESPACE}.RankBoardService")
