# project_service_declare_protocol(
#   "ranksvr-protocol-settlement" "${CMAKE_CURRENT_LIST_DIR}/protocol/settlement"
#   PROTOCOLS "${CMAKE_CURRENT_LIST_DIR}/protocol/settlement/protocol/pbdesc/rank_settlement_service.proto")


add_subdirectory(sdk)
add_subdirectory(service/settlement)

