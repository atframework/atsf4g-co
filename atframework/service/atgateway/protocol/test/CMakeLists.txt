# Copyright 2026 atframework
echowithcolor(COLOR GREEN "-- Configure Unit Test ${CMAKE_CURRENT_LIST_DIR}")

include("${PROJECT_TEST_BAS_DIR}/test.build_bin.cmake")

file(
  GLOB_RECURSE
  PROJECT_TEST_SRC_LIST
  ${PROJECT_TEST_SRC_DIR}/app/*.cpp
  ${PROJECT_TEST_SRC_DIR}/frame/*.h
  ${PROJECT_TEST_SRC_DIR}/frame/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/*.hpp
  ${CMAKE_CURRENT_LIST_DIR}/*.h
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/*.cc
  ${CMAKE_CURRENT_LIST_DIR}/*.cxx)
source_group_by_dir(PROJECT_TEST_SRC_LIST)

set(CMAKE_BUILD_RPATH "${CMAKE_INSTALL_RPATH}")
if(NOT (WIN32 AND BUILD_SHARED_LIBS))
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test")
endif()

atframe_add_test_executable(atgateway_protocol_unit_test ${PROJECT_TEST_SRC_LIST})

target_link_libraries(atgateway_protocol_unit_test ${ATFRAMEWORK_SERVICE_GATEWAY_CLIENT_SDK}
                      ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})

add_test(NAME "atgateway_protocol.unit_test" COMMAND "$<TARGET_FILE:atgateway_protocol_unit_test>")
set_tests_properties("atgateway_protocol.unit_test" PROPERTIES LABELS "atgateway;atgateway_protocol.unit_test")
