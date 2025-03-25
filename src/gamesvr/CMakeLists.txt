project_service_declare_protocol("gamesvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/game_service.proto")

add_subdirectory(sdk)

add_subdirectory(service)
