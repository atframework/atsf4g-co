project_service_declare_protocol("rank-settlement-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/rank_settlement_config.proto")

add_subdirectory(service)