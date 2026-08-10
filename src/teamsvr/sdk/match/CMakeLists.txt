# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(CURRENT_SERVICE_SOURCES rpc/team/teammatchservice.atfw.gen.cpp)
set(CURRENT_SERVICE_HEADERS rpc/team/teammatchservice.atfw.gen.h)

project_service_declare_sdk(
  "team-sdk-match"
  "${CMAKE_CURRENT_LIST_DIR}"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  TEAM_SDK_MATCH_API
  USE_SERVICE_PROTOCOL
  "team-protocol-match-pbdesc"
  USE_SERVICE_SDK
  "team-common-sdk"
  HEADERS
  ${CURRENT_SERVICE_HEADERS}
  SOURCES
  ${CURRENT_SERVICE_SOURCES}
  GENERATED_FLOW_NAMES
  "atframework.team.TeamMatchService")
