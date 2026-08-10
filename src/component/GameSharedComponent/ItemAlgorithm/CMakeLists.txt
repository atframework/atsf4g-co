# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_game_shared_component_declare_sdk(
  ItemAlgorithmSDK
  "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_TARGET_NAME
  TARGET_FULL_NAME
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/include"
  DLLEXPORT_DECL
  ITEM_ALGORITHM_API
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemAlgorithmConfig.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemGridAlgorithm.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemGridContainer.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemGridData.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemGridAlgorithm.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemGridContainer.cpp")

target_link_libraries(${TARGET_FULL_NAME} PRIVATE xxHash::xxhash)
