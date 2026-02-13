project_component_declare_protocol(
  "distributed-transaction-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" OUTPUT_TARGET_NAME
  PROJECT_COMPONENT_DISTRIBUTED_TRANSACTION_PROTOCOL PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/distributed_transaction.proto")

project_component_declare_protocol("dtcoordsvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                   "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/dtcoordsvr_config.proto")

target_include_directories(${PROJECT_COMPONENT_DISTRIBUTED_TRANSACTION_PROTOCOL}
                           PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

add_subdirectory(sdk)

add_subdirectory(dtcoordsvr)
