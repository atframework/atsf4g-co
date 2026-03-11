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

add_subdirectory(sdk)
add_subdirectory(rank_board_svr)