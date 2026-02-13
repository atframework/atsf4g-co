# ============ sample - [...] ============

get_filename_component(SAMPLE_SRC_BIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME_WE)
set(SAMPLE_SRC_BIN_NAME "sample_${SAMPLE_SRC_BIN_NAME}")
echowithcolor(COLOR GREEN "-- Configure ${SAMPLE_SRC_BIN_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SAMPLE_SRC_LIST)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/sample")

add_executable(${SAMPLE_SRC_BIN_NAME} ${SAMPLE_SRC_LIST})

target_link_libraries(${SAMPLE_SRC_BIN_NAME} atgateway_client_sdk_v2_c)

target_compile_options(${SAMPLE_SRC_BIN_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})

if(MSVC)
  set_property(TARGET "${SAMPLE_SRC_BIN_NAME}" PROPERTY FOLDER "${PROJECT_NAME}")
endif()
