﻿# ============ sample - [...] ============
get_filename_component(SERVER_INST_SRC_BIN_NAME ${CMAKE_CURRENT_LIST_DIR} NAME_WE)
EchoWithColor(COLOR GREEN "-- Configure ${SERVER_INST_SRC_BIN_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

set(EXECUTABLE_OUTPUT_PATH "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/bin")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/bin")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/etc")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${SERVER_INST_SRC_BIN_NAME}/log")

# ============ atgateway ============

file(GLOB_RECURSE SERVER_INST_SRC_LIST
    *.cpp
    *.cc
    *.c
    *.cxx
    *.h
    *.hpp
)
source_group_by_dir(SERVER_INST_SRC_LIST)

if (MINGW)
    list(APPEND EXTENTION_LINK_LIB stdc++)
endif()

# these service will use atgateway protocol
include_directories(${ATFRAMEWORK_SERVICE_GATEWAY_PROTOCOL_DIR})

add_executable(${SERVER_INST_SRC_BIN_NAME} ${SERVER_INST_SRC_LIST} ${PROJECT_3RD_PARTY_SRC_LIST})
target_link_libraries(${SERVER_INST_SRC_BIN_NAME}
    ${PROJECT_LIB_LINK}
    ${ATFRAMEWORK_SERVICE_LINK_NAME}
    ${ATFRAMEWORK_ATAPP_LINK_NAME}
    ${ATFRAMEWORK_ATBUS_LINK_NAME}
    ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME}
    ${3RD_PARTY_LIBUV_LINK_NAME}
    ${EXTENTION_LINK_LIB}
)