# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(CURRENT_SERVICE_SOURCES rpc/friends/friendrecommendservice.atfw.gen.cpp)
set(CURRENT_SERVICE_HEADERS rpc/friends/friendrecommendservice.atfw.gen.h)

project_service_declare_sdk(
  "friend-sdk-recommend"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  FRIEND_SDK_RECOMMEND_API
  USE_SERVICE_PROTOCOL
  "friend-protocol-recommend-pbdesc"
  HEADERS
  ${CURRENT_SERVICE_HEADERS}
  SOURCES
  ${CURRENT_SERVICE_SOURCES}
  GENERATED_FLOW_NAMES
  "atframework.friends.FriendRecommendService")
