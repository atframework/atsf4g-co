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
  INSTALL_PREBUILD_COMPONENT
  "GameSharedComponent-Utility-Prebuild"
  USE_PROTOCOLS
  "Config"
  "Network"
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemAlgorithmConfig.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemAlgorithmLog.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemContainer.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemContainerEntry.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemContainerGroup.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemContainerTypes.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemIterable.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/Container/ItemFiniteGridContainer.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/Container/ItemInfiniteGridContainer.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/Container/ItemNoPositionContainer.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/ItemAlgorithm/ItemGridOccupancy.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemAlgorithmLog.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemContainer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemContainerEntry.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemContainerGroup.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemContainerTypes.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/Container/ItemFiniteGridContainer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/Container/ItemFiniteGridContainerOperation.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/Container/ItemFiniteGridFindPosition.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemContainerOperation.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/Container/ItemInfiniteGridContainer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/Container/ItemInfiniteGridContainerOperation.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/Container/ItemNoPositionContainer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/Container/ItemNoPositionContainerOperation.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/ItemAlgorithm/ItemGridOccupancy.cpp")

target_link_libraries(${TARGET_FULL_NAME} PRIVATE xxHash::xxhash)

if(TARGET ${PROJECT_NAME}::ConfigLoader)
  target_link_libraries(${TARGET_FULL_NAME} PRIVATE ${PROJECT_NAME}::ConfigLoader)
elseif(PROJECT_SERVER_FRAME_LIB_LINK AND TARGET ${PROJECT_SERVER_FRAME_LIB_LINK}-config)
  target_link_libraries(${TARGET_FULL_NAME} PRIVATE ${PROJECT_SERVER_FRAME_LIB_LINK}-config)
endif()
