include("${CMAKE_CURRENT_LIST_DIR}/atframework.internal-functions.cmake")
atframework_write_package_config("${ATFRAMEWORK_EXPORT_PACKAGE_NAME}")

# Basic services
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/service")

# Export
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/export")
