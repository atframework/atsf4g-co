# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_game_shared_component_declare_sdk(
  BattleUtilitySDK
  "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_TARGET_NAME
  TARGET_FULL_NAME
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/include"
  DLLEXPORT_DECL
  BATTLE_UTILITY_API
  INSTALL_PREBUILD_COMPONENT
  "GameSharedComponent-Utility-Prebuild"
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/include/BattleUtility/BattleUtilityConfig.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Random/Random.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/src/Random/Random.cpp"
)
