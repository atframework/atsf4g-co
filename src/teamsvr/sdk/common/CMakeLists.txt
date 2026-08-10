# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(TEAM_SDK_COMMON_SOURCES rpc/team/team_common_api.cpp)
set(TEAM_SDK_COMMON_HEADERS rpc/team/team_common_api.h)

project_service_declare_sdk(
  "team-common-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  TEAM_SDK_COMMON_API
  # USE_COMPONENTS "team-sdk-common" # 公共协议
  HEADERS
  ${TEAM_SDK_COMMON_HEADERS}
  SOURCES
  ${TEAM_SDK_COMMON_SOURCES})
