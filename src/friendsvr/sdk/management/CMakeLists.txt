# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(CURRENT_SERVICE_SOURCES rpc/friends/friendmanagementservice.atfw.gen.cpp data/friend_cache.cpp
                            router/router_friend_cache.cpp router/router_friend_manager.cpp)
set(CURRENT_SERVICE_HEADERS rpc/friends/friendmanagementservice.atfw.gen.h data/friend_cache.h
                            router/router_friend_cache.h router/router_friend_manager.h)

project_service_declare_sdk(
  "friend-sdk-management"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  FRIEND_SDK_MANAGEMENT_API
  SHARED_LIBRARY_DECL
  FRIEND_SDK_MANAGEMENT_DLL
  NATIVE_CODE_DECL
  FRIEND_SDK_MANAGEMENT_NATIVE
  USE_SERVICE_PROTOCOL
  "friend-protocol-management-pbdesc"
  HEADERS
  ${CURRENT_SERVICE_HEADERS}
  SOURCES
  ${CURRENT_SERVICE_SOURCES}
  GENERATED_FLOW_NAMES
  "atframework.friends.FriendManagementService")
