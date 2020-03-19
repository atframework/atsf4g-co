

EchoWithColor(COLOR GREEN "-- Configure ${CMAKE_CURRENT_LIST_DIR}")

include ("${CMAKE_CURRENT_LIST_DIR}/sample.custom-macro.cmake")

# ============ sample - [...] ============
file(GLOB SAMPLE_BIN_LIST LIST_DIRECTORIES true "${CMAKE_CURRENT_LIST_DIR}/*")

if(NOT EXISTS "${PROJECT_BINARY_DIR}/sample")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/sample")
endif()

foreach(SAMPLE_BIN_DIR IN LISTS SAMPLE_BIN_LIST)
    if(IS_DIRECTORY ${SAMPLE_BIN_DIR} AND EXISTS "${SAMPLE_BIN_DIR}/CMakeLists.txt")
        add_subdirectory(${SAMPLE_BIN_DIR})
    endif()
endforeach()
