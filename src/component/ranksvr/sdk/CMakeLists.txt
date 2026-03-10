
project_component_declare_sdk(
  "rank-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  DLLEXPORT_DECL
  RANK_SDK_API
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/common/"
  OUTPUT_TARGET_NAME
  PROJECT_COMMON_RANK_SDK
  HRADERS
  "${CMAKE_CURRENT_LIST_DIR}/common/rank/logic_rank_handle.h"
  "${CMAKE_CURRENT_LIST_DIR}/common/rank/logic_rank_algorithm.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/common/rank/logic_rank_handle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/common/rank/logic_rank_algorithm.cpp")

  
project_component_declare_sdk(
  "ranksvr-sdk-ranking"
  "${CMAKE_CURRENT_LIST_DIR}/ranking"
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/ranking"
  DLLEXPORT_DECL
  RANK_RPC_API
  OUTPUT_TARGET_NAME
  PROJECT_COMPONENT_RANKING_SDK
  HRADERS
  "${CMAKE_CURRENT_LIST_DIR}/ranking/rpc/rank/ranksvrservice.h"
  "${CMAKE_CURRENT_LIST_DIR}/ranking/rpc/rank/rank.h"
  "${CMAKE_CURRENT_LIST_DIR}/ranking/rpc/rank/sharding.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/ranking/rpc/rank/ranksvrservice.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/ranking/rpc/rank/rank.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/ranking/rpc/rank/sharding.cpp")

target_link_libraries(${PROJECT_COMMON_RANK_SDK} PUBLIC components::ranksvr-sdk-ranking)

target_link_libraries(${PROJECT_COMPONENT_RANKING_SDK} PUBLIC components::ranksvr-protocol-ranking)


# project_service_declare_sdk(
#   "ranksvr-sdk-settlement"
#   "${CMAKE_CURRENT_LIST_DIR}/settlement"
#   INCLUDE_DIR
#   "${CMAKE_CURRENT_LIST_DIR}/settlement"
#   DLLEXPORT_DECL
#   RANK_RPC_API
#   # USE_SERVICE_PROTOCOL
#   # ranksvr-protocol-settlement
#   HRADERS
#   "${CMAKE_CURRENT_LIST_DIR}/settlement/rpc/rank/ranksvrsettlementservice.h"
#   SOURCES
#   "${CMAKE_CURRENT_LIST_DIR}/settlement/rpc/rank/ranksvrsettlementservice.cpp"
# )

# target_link_libraries(${PROJECT_COMPONENT_RANKSVR_SDK} PUBLIC components::ranksvr-protocol)
