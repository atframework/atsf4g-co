# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

get_filename_component(SERVICE_TARGET_NAME ${CMAKE_CURRENT_LIST_DIR} NAME_WE)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_component_declare_service(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_COMPONENTS
  "distributed-transaction-protocol"
  "dtcoordsvr-config"
  GENERATED_FLOW_NAMES
  "atframework.distributed_system.DtcoordsvrService")
