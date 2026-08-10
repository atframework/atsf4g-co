# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_component_declare_protocol(
  "rank-board-svr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" 
  OUTPUT_TARGET_NAME
  RANK_BOARD_SVR_PROTOCOL
  PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/rank_board_service.proto")

project_component_declare_protocol("rank-board-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/rank_board_config.proto")

target_include_directories(${RANK_BOARD_SVR_PROTOCOL}
                           PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.RankBoardService"
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr"
  TASK_PATH_PREFIX
  "logic"
  HANDLE_PATH_PREFIX
  "app"
  PROJECT_NAMESPACE
  "${PROJECT_NAMESPACE}"
  RPC_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/sdk/rank_board_svr"
  SERVICE_DLLEXPORT_DECL
  RANK_BOARD_SERVICE_API
  RPC_DLLEXPORT_DECL
  RANK_BOARD_SDK_API
  EXTERNAL_COMPONENT_PROTOCOLS
  "rank-board-svr-protocol"
  INCLUDE_HEADERS
  "protocol/pbdesc/rank_board_service.pb.h")

add_subdirectory(sdk)
add_subdirectory(rank_board_svr)

if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()