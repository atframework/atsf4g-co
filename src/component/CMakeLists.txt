include("${CMAKE_CURRENT_LIST_DIR}/component-functions.cmake")

# ============ components ============
add_subdirectory(test)
add_subdirectory(distributed_transaction)

# ============ Export and install components ============
install(
  EXPORT ${PROJECT_INSTALL_COMPONENT_EXPORT_NAME}
  NAMESPACE "${PROJECT_NAME}::component::"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
