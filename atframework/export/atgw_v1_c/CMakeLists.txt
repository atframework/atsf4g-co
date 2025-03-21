include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT ATFRAMEWORK_EXPORT_TARGET_NAME)
  set(ATFRAMEWORK_EXPORT_TARGET_NAME "atframework-target")
endif()

# ============ export - [...] ============
get_filename_component(EXPORT_SRC_BIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME_WE)

echowithcolor(COLOR GREEN "-- Configure ${EXPORT_SRC_BIN_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} EXPORT_SRC_LIST)

atframework_add_library(
  ${EXPORT_SRC_BIN_NAME}
  PUBLIC_SYMBOL_DECL
  LIBATGW_V1_C_API
  FOLDER_PATH
  "atframework/sdk"
  PUBLIC_INCLUDE_DIRECTORY
  "${CMAKE_CURRENT_LIST_DIR}"
  INSTALL_HEADER_DIRECTORY
  "${CMAKE_CURRENT_LIST_DIR}"
  PUBLIC_LINK_NAMES
  ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}
  PRIVATE_LINK_NAMES
  ${ATFRAMEWORK_SERVICE_GATEWAY_CLIENT_SDK}
  HEADERS
  "libatgw_v1_c.h"
  SOURCES
  "libatgw_v1_c.cpp")
