project_component_declare_protocol(
  "ranksvr-protocol-ranking" "${CMAKE_CURRENT_LIST_DIR}/protocol/ranking" 
  OUTPUT_TARGET_NAME
  RANKSVR_PROTOCOL_RANKING_PROTOCOL
  PROTOCOLS 
  "${CMAKE_CURRENT_LIST_DIR}/protocol/ranking/protocol/pbdesc/rank_service.proto")

target_include_directories(${RANKSVR_PROTOCOL_RANKING_PROTOCOL}
                           PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

add_subdirectory(sdk)
add_subdirectory(service/ranking)