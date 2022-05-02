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

# these service will use atgateway protocol

add_executable(${SERVER_INST_SRC_BIN_NAME} ${SERVER_INST_SRC_LIST})
if(MSVC)
  add_target_properties(${SERVER_INST_SRC_BIN_NAME} LINK_FLAGS /NODEFAULTLIB:library)
endif()

target_link_libraries(${SERVER_INST_SRC_BIN_NAME} ${ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME}
                      ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME})

target_compile_options(${SERVER_INST_SRC_BIN_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})

if(MSVC)
  set_property(TARGET ${SERVER_INST_SRC_BIN_NAME} PROPERTY FOLDER "${PROJECT_NAME}/service/server")
endif()

project_install_and_export_targets(${SERVER_INST_SRC_BIN_NAME})
