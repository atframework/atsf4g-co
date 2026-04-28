project_service_declare_protocol("cachesvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/cache_service.proto")

add_subdirectory(sdk)

add_subdirectory(service)
