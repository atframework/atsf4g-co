include_guard(GLOBAL)

set(ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR
    "${PROJECT_SOURCE_DIR}/atframework/atframe_utils"
    CACHE PATH "PATH to atframe_utils")

set(ATFRAMEWORK_LIBATBUS_REPO_DIR
    "${PROJECT_SOURCE_DIR}/atframework/libatbus"
    CACHE PATH "PATH to libatbus")

set(ATFRAMEWORK_LIBATAPP_REPO_DIR
    "${PROJECT_SOURCE_DIR}/atframework/libatapp"
    CACHE PATH "PATH to libatbus")

macro(ATFRAMEWORK_ATFRAME_UTILS_POPULATE)
  if(NOT EXISTS "${ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR}/CMakeLists.txt")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --depth 100 --recommend-shallow -f --init -- atframework/atframe_utils
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" ${ATFRAMEWORK_CMAKE_TOOLSET_EXECUTE_PROCESS_OUTPUT_OPTIONS})
    set(ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR
        "${PROJECT_SOURCE_DIR}/atframework/atframe_utils"
        CACHE PATH "PATH to atframe_utils" FORCE)
  endif()
endmacro()

if(TARGET atframe_utils)
  set(ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME atframe_utils)
elseif(TARGET atframework::atframe_utils)
  set(ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME atframework::atframe_utils)
else()
  set(ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME atframe_utils)
  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
  endif()
  atframework_atframe_utils_populate()
  add_subdirectory("${ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}")
endif()

macro(ATFRAMEWORK_LIBATBUS_POPULATE)
  if(NOT EXISTS "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/CMakeLists.txt")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --depth 100 --recommend-shallow -f --init -- atframework/libatbus
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" ${ATFRAMEWORK_CMAKE_TOOLSET_EXECUTE_PROCESS_OUTPUT_OPTIONS})
    set(ATFRAMEWORK_LIBATBUS_REPO_DIR
        "${PROJECT_SOURCE_DIR}/atframework/libatbus"
        CACHE PATH "PATH to libatbus" FORCE)
  endif()
endmacro()

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
  atframework_libatbus_populate()
  add_subdirectory("${ATFRAMEWORK_LIBATBUS_REPO_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATBUS_LINK_NAME}")
endif()

macro(ATFRAMEWORK_LIBATAPP_POPULATE)
  if(NOT EXISTS "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/CMakeLists.txt")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --depth 100 --recommend-shallow -f --init -- atframework/libatapp
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" ${ATFRAMEWORK_CMAKE_TOOLSET_EXECUTE_PROCESS_OUTPUT_OPTIONS})
    set(ATFRAMEWORK_LIBATAPP_REPO_DIR
        "${PROJECT_SOURCE_DIR}/atframework/libatapp"
        CACHE PATH "PATH to libatapp" FORCE)
  endif()
endmacro()

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
  atframework_libatapp_populate()
  add_subdirectory("${ATFRAMEWORK_LIBATAPP_REPO_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/_deps/${ATFRAMEWORK_LIBATAPP_LINK_NAME}")
endif()
