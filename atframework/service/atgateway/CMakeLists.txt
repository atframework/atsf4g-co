set(ATSF4G_APP_NAME atgateway)
echowithcolor(COLOR GREEN "-- Configure ${ATSF4G_APP_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

add_subdirectory(protocol)

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

set_target_properties(${ATSF4G_APP_NAME} PROPERTIES OUTPUT_NAME "atgatewayd")

# ============ benchmark client tool ============
if(PROJECT_ENABLE_TOOLS)
  set(ATSF4G_APP_BENCHMARK_CLIENT_NAME atgateway_benchmark_client)
  echowithcolor(COLOR GREEN "-- Configure ${ATSF4G_APP_BENCHMARK_CLIENT_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

  add_executable(${ATSF4G_APP_BENCHMARK_CLIENT_NAME})
  target_sources(${ATSF4G_APP_BENCHMARK_CLIENT_NAME}
                 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/tools/atgateway_benchmark_client.cpp")
  target_include_directories(${ATSF4G_APP_BENCHMARK_CLIENT_NAME}
                             PRIVATE "${CMAKE_CURRENT_LIST_DIR}"
                                     "$<TARGET_PROPERTY:${ATFRAMEWORK_SERVICE_GATEWAY_CLIENT_SDK},INTERFACE_INCLUDE_DIRECTORIES>")
  target_link_libraries(${ATSF4G_APP_BENCHMARK_CLIENT_NAME} PRIVATE ${ATFRAMEWORK_SERVICE_GATEWAY_CLIENT_SDK}
                                                                    ${ATFRAMEWORK_LIBATAPP_LINK_NAME})
  set_target_properties(${ATSF4G_APP_BENCHMARK_CLIENT_NAME} PROPERTIES FOLDER "atframework/atgateway/service")
endif()

# ============ unit tests ============
if(PROJECT_ENABLE_UNITTEST)
  atframework_atframe_utils_populate()
  include("${ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR}/test/test.utils-macro.cmake")
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/test")
endif()
