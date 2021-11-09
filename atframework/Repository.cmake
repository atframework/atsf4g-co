include_guard(GLOBAL)

if(TARGET atframe_utils)
  set(ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME atframe_utils)
elseif(TARGET atframework::atframe_utils)
  set(ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME atframework::atframe_utils)
else()
  set(ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME atframe_utils)
  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
  endif()
  maybe_populate_submodule(ATFRAMEWORK_ATFRAME_UTILS "atframework/atframe_utils"
                           "${PROJECT_SOURCE_DIR}/atframework/atframe_utils")
  add_subdirectory("${ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
endif()

if(TARGET atbus)
  set(ATFRAMEWORK_LIBATBUS_LINK_NAME atbus)
  set(ATFRAMEWORK_LIBATBUS_PROTOCOL_LINK_NAME atbus-protocol)
elseif(TARGET atframework::atbus)
  set(ATFRAMEWORK_LIBATBUS_LINK_NAME atframework::atbus)
  set(ATFRAMEWORK_LIBATBUS_PROTOCOL_LINK_NAME atframework::atbus-protocol)
else()
  set(ATFRAMEWORK_LIBATBUS_LINK_NAME atbus)
  set(ATFRAMEWORK_LIBATBUS_PROTOCOL_LINK_NAME atbus-protocol)
  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATBUS_LINK_NAME}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATBUS_LINK_NAME}")
  endif()
  maybe_populate_submodule(ATFRAMEWORK_LIBATBUS "atframework/libatbus" "${PROJECT_SOURCE_DIR}/atframework/libatbus")
  add_subdirectory("${ATFRAMEWORK_LIBATBUS_REPO_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATBUS_LINK_NAME}")
endif()

if(TARGET atapp)
  set(ATFRAMEWORK_LIBATAPP_LINK_NAME atapp)
  set(ATFRAMEWORK_LIBATAPP_PROTOCOL_LINK_NAME atapp-protocol)
elseif(TARGET atframework::atapp)
  set(ATFRAMEWORK_LIBATAPP_LINK_NAME atframework::atapp)
  set(ATFRAMEWORK_LIBATAPP_PROTOCOL_LINK_NAME atframework::atapp-protocol)
else()
  set(ATFRAMEWORK_LIBATAPP_LINK_NAME atapp)
  set(ATFRAMEWORK_LIBATAPP_PROTOCOL_LINK_NAME atapp-protocol)
  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATAPP_LINK_NAME}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATAPP_LINK_NAME}")
  endif()
  maybe_populate_submodule(ATFRAMEWORK_LIBATAPP "atframework/libatapp" "${PROJECT_SOURCE_DIR}/atframework/libatapp")
  add_subdirectory("${ATFRAMEWORK_LIBATAPP_REPO_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATAPP_LINK_NAME}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/atframework.macro.cmake")

# =========== third_party - hiredis-happ ===========
include("${CMAKE_CURRENT_LIST_DIR}/../third_party/redis/redis.cmake")
list(PREPEND PROJECT_THIRD_PARTY_PUBLIC_LINK_NAMES
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_HAPP_LINK_NAME})

