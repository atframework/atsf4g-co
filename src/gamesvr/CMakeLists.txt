# ============ sample - [...] ============
get_filename_component(SERVER_INST_SRC_BIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME_WE)
echowithcolor(COLOR GREEN "-- Configure ${SERVER_INST_SRC_BIN_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/bin")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/bin")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/etc")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/log")

# ============ atgateway ============

file(
  GLOB_RECURSE
  SERVER_INST_SRC_LIST
  *.cpp
  *.cc
  *.c
  *.cxx
  *.h
  *.hpp)
source_group_by_dir(SERVER_INST_SRC_LIST)

add_executable("${SERVER_INST_SRC_BIN_NAME}d" ${SERVER_INST_SRC_LIST})
target_link_libraries("${SERVER_INST_SRC_BIN_NAME}d" ${PROJECT_SERVER_FRAME_LIB_LINK} components::test-api)

target_include_directories("${SERVER_INST_SRC_BIN_NAME}d" PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>")

target_compile_options("${SERVER_INST_SRC_BIN_NAME}d" PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})

project_install_and_export_targets("${SERVER_INST_SRC_BIN_NAME}d")
