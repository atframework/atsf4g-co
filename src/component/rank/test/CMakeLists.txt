# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# ============ Rank component unit tests (rpc-unit-test based) ============

project_add_rpc_unit_test(
  TARGET ${PROJECT_NAME}-component-rank-unit-test
  COMPONENT component-rank
  SOURCES "${CMAKE_CURRENT_LIST_DIR}/rank_test_sdk_update_score.cpp"
  LINK_LIBRARIES components::rank-board-svr-sdk
  FEATURES SS
  LABELS fast
  TIMEOUT 120)
