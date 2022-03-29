project_component_declare_sdk(
  "distributed-transaction-sdk"
  "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_TARGET_NAME
  PROJECT_COMPONENT_DISTRIBUTED_TRANSACTION_SDK
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}"
  HRADERS
  "${CMAKE_CURRENT_LIST_DIR}/rpc/transaction/dtcoordsvrservice.h"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/transaction/transaction_api.h"
  "${CMAKE_CURRENT_LIST_DIR}/transaction_participator_handle.h"
  "${CMAKE_CURRENT_LIST_DIR}/transaction_client_handle.h"
  "${CMAKE_CURRENT_LIST_DIR}/logic/action/task_action_participator_resolve_transaction.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/rpc/transaction/dtcoordsvrservice.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/rpc/transaction/transaction_api.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/transaction_participator_handle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/transaction_client_handle.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/logic/action/task_action_participator_resolve_transaction.cpp")

target_link_libraries(${PROJECT_COMPONENT_DISTRIBUTED_TRANSACTION_SDK}
                      PUBLIC components::distributed-transaction-protocol)
