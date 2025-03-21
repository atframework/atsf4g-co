set(ATSF4G_APP_NAME atgateway)
echowithcolor(COLOR GREEN "-- Configure ${ATSF4G_APP_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

add_subdirectory(protocols)

# ============ atgateway ============

set(ATSF4G_APP_HEADER_LIST "session.h" "session_manager.h")
set(ATSF4G_APP_SOURCE_LIST "atgateway.cpp" "session.cpp" "session_manager.cpp")

atframework_add_executable(
  ${ATSF4G_APP_NAME}
  ENABLE_PRIVATE_PRECOMPILE_HEADERS
  FOLDER_PATH
  "atframework/atgateway/service"
  PUBLIC_INCLUDE_DIRECTORY
  "${CMAKE_CURRENT_LIST_DIR}"
  PRIVATE_LINK_NAMES
  ${ATFRAMEWORK_SERVICE_GATEWAY_SERVER_SDK}
  ${ATFRAMEWORK_SERVICE_GATEWAY_CLIENT_SDK}
  atgateway-server-config
  ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME}
  ${ATFRAMEWORK_LIBATAPP_LINK_NAME}
  HEADERS
  ${ATSF4G_APP_HEADER_LIST}
  SOURCES
  ${ATSF4G_APP_SOURCE_LIST})
