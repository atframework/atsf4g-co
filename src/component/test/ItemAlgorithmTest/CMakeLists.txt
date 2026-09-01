# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ ItemAlgorithm component unit tests ============

project_add_normal_unit_test(
  TARGET
  ${PROJECT_NAME}-component-item-algorithm-unit-test
  COMPONENT
  component-item-algorithm
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/item_grid_algorithm_test.cpp"
  LINK_LIBRARIES
  components::ItemAlgorithmSDK
  COMPILE_DEFINITIONS
  ${PROJECT_GAME_SHARED_COMPONENT_PRIVATE_DEFINITIONS}
  LABELS
  fast)
