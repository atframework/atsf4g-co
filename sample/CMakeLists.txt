echowithcolor(COLOR GREEN "-- Configure ${CMAKE_CURRENT_LIST_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/sample.custom-macro.cmake")

# ============ sample - [...] ============
set(SAMPLE_BIN_LIST atgateway_client_hello)

if(NOT EXISTS "${PROJECT_BINARY_DIR}/sample")
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/sample")
endif()

foreach(SAMPLE_BIN_DIR IN LISTS SAMPLE_BIN_LIST)
  set(SAMPLE_BIN_PATH "${CMAKE_CURRENT_LIST_DIR}/${SAMPLE_BIN_DIR}")
  if(IS_DIRECTORY "${SAMPLE_BIN_PATH}" AND EXISTS "${SAMPLE_BIN_PATH}/CMakeLists.txt")
    add_subdirectory(${SAMPLE_BIN_DIR})
  endif()
endforeach()
