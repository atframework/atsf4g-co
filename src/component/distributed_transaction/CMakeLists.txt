project_component_declare_protocol(
  "distributed-transaction-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" OUTPUT_TARGET_NAME
  PROJECT_COMPONENT_DISTRIBUTED_TRANSACTION_PROTOCOL PROTOCOLS
  "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/distributed_transaction.proto")

target_include_directories(
  ${PROJECT_COMPONENT_DISTRIBUTED_TRANSACTION_PROTOCOL}
  PUBLIC "$<BUILD_INTERFACE:${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/include>"
         "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

add_subdirectory(sdk)

add_subdirectory(dtcoordsvr)
