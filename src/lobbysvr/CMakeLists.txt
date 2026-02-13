project_service_declare_protocol("lobbysvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/lobby_service.proto")
project_service_declare_protocol("lobbysvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/lobbysvr_config.proto")

add_subdirectory(sdk)

add_subdirectory(service)
