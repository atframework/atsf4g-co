project_component_declare_protocol(
  "ranksvr-protocol" "${CMAKE_CURRENT_LIST_DIR}/protocol" 
  OUTPUT_TARGET_NAME
  RANKSVR_PROTOCOL_RANKSVR_PROTOCOL
  PROTOCOLS 
  "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/pbdesc/rank_service.proto")

project_component_declare_protocol("ranksvr-config" "${CMAKE_CURRENT_LIST_DIR}/protocol" PROTOCOLS
                                 "${CMAKE_CURRENT_LIST_DIR}/protocol/protocol/config/ranksvr_config.proto")

target_include_directories(${RANKSVR_PROTOCOL_RANKSVR_PROTOCOL}
                           PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

add_subdirectory(sdk)
add_subdirectory(ranksvr)