
project_component_declare_sdk(
  "rank-sdk"
  "${CMAKE_CURRENT_LIST_DIR}/rank"
  DLLEXPORT_DECL
  RANK_SDK_API
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/rank/"
  OUTPUT_TARGET_NAME
  PROJECT_COMMON_RANK_SDK
  HRADERS
  "${CMAKE_CURRENT_LIST_DIR}/rank/rank/logic_rank_handle.h"
  "${CMAKE_CURRENT_LIST_DIR}/rank/rank/logic_rank_algorithm.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/rank/rank/logic_rank_handle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rank/rank/logic_rank_algorithm.cpp")

project_component_declare_sdk(
  "ranksvr-sdk"
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr"
  DLLEXPORT_DECL
  RANK_RPC_API
  OUTPUT_TARGET_NAME
  PROJECT_COMPONENT_RANKSVR_SDK
  HRADERS
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr/rpc/rank/ranksvrservice.h"
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr/rpc/rank/rank.h"
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr/rpc/rank/sharding.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr/rpc/rank/ranksvrservice.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr/rpc/rank/rank.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/ranksvr/rpc/rank/sharding.cpp")

target_link_libraries(${PROJECT_COMMON_RANK_SDK} PUBLIC components::ranksvr-sdk)
target_link_libraries(${PROJECT_COMPONENT_RANKSVR_SDK} PUBLIC components::ranksvr-protocol)
