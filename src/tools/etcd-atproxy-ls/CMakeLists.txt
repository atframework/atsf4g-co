# ============ scan services fo atproxy - [...] ============
get_filename_component(TOOL_SRC_DIR_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
set(TOOL_SRC_BIN_NAME "${TOOL_SRC_DIR_NAME}-cli")
echowithcolor(COLOR GREEN "-- Configure ${TOOL_SRC_BIN_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SIMULATOR_SRC_LIST)
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/tools/${TOOL_SRC_DIR_NAME}/bin")

# build libserver_frame.so
file(
  GLOB_RECURSE
  SRC_LIST
  ${CMAKE_CURRENT_LIST_DIR}/*.h
  ${CMAKE_CURRENT_LIST_DIR}/*.hpp
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/*.cc)

source_group_by_dir(SRC_LIST)

# these service will use atgateway protocol

add_executable(${TOOL_SRC_BIN_NAME} ${SRC_LIST})
project_tool_split_target_debug_sybmol("${TOOL_SRC_BIN_NAME}")
project_tool_set_target_runtime_output_directory("${PROJECT_INSTALL_BAS_DIR}/tools/${TOOL_SRC_DIR_NAME}/bin"
                                                 ${TOOL_SRC_BIN_NAME} WITH_TARGET_RPATH WITH_ARCHIVE_RPATH)

set_property(TARGET ${TOOL_SRC_BIN_NAME} PROPERTY FOLDER "${PROJECT_NAME}/tools")
if(MSVC)
  add_target_properties(${TOOL_SRC_BIN_NAME} LINK_FLAGS /NODEFAULTLIB:library)
endif()

target_link_libraries(${TOOL_SRC_BIN_NAME} ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME})

target_include_directories(${TOOL_SRC_BIN_NAME} PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>")

project_install_and_export_targets(${TOOL_SRC_BIN_NAME})
