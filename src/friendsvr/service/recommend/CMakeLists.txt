# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(SERVICE_TARGET_NAME friendsvr-recommend)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_service_declare_instance(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  HEADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  MAIN_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/app/friend_recommend_main.cpp"
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_SERVICE_SDK
  "friend-sdk-recommend"
  USE_SERVICE_PROTOCOL
  "friend-protocol-recommend-config"
  GENERATED_FLOW_NAMES
  "atframework.friend_api.FriendRecommendService")
