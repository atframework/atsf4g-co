project_service_declare_protocol("rank-settlement-config" "${CMAKE_CURRENT_LIST_DIR}/protocol/settlement" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/settlement/protocol/config/rank_settlement_config.proto")

add_subdirectory(sdk)
add_subdirectory(service/settlement)

