project_component_declare_sdk(
  "rank-board-svr-sdk"
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr"
  DLLEXPORT_DECL
  RANK_BOARD_SDK_API
  OUTPUT_TARGET_NAME
  PROJECT_COMPONENT_RANK_BOARD_SVR_SDK
  HRADERS
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr/rpc/rank_board/rankboardservice.h"
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr/rpc/rank_board/rank.h"
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr/rpc/rank_board/sharding.h"
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr/rpc/rank_board/common.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr/rpc/rank_board/rankboardservice.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr/rpc/rank_board/rank.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rank_board_svr/rpc/rank_board/sharding.cpp")

target_link_libraries(${PROJECT_COMPONENT_RANK_BOARD_SVR_SDK} PUBLIC components::rank-board-svr-protocol)

project_component_declare_sdk(
  "rank-logic-sdk"
  "${CMAKE_CURRENT_LIST_DIR}/rank_logic"
  DLLEXPORT_DECL
  RANK_LOGIC_SDK_API
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/rank_logic/"
  OUTPUT_TARGET_NAME
  PROJECT_COMMON_RANK_LOGIC_SDK
  USE_COMPONENTS
  "rank-board-svr-sdk"
  HRADERS
  "${CMAKE_CURRENT_LIST_DIR}/rank_logic/rank_logic/logic_rank_handle.h"
  "${CMAKE_CURRENT_LIST_DIR}/rank_logic/rank_logic/logic_rank_algorithm.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/rank_logic/rank_logic/logic_rank_handle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rank_logic/rank_logic/logic_rank_algorithm.cpp")
