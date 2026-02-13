project_service_declare_protocol("authsvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/authsvr_config.proto")

add_subdirectory(service)
