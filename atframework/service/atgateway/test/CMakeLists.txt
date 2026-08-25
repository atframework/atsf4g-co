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
list(APPEND PROJECT_TEST_SRC_LIST "${CMAKE_CURRENT_LIST_DIR}/../session.cpp"
     "${CMAKE_CURRENT_LIST_DIR}/../session_manager.cpp")
source_group_by_dir(PROJECT_TEST_SRC_LIST)

set(CMAKE_BUILD_RPATH "${CMAKE_INSTALL_RPATH}")
if(NOT (WIN32 AND BUILD_SHARED_LIBS))
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test")
endif()

atframe_add_test_executable(atgateway-unit_test ${PROJECT_TEST_SRC_LIST})

target_include_directories(atgateway-unit_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")

target_link_libraries(
  atgateway-unit_test
  ${ATFRAMEWORK_SERVICE_GATEWAY_SERVER_SDK}
  atgateway-server-config
  ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME}
  ${ATFRAMEWORK_LIBATAPP_LINK_NAME}
  ${ATFRAMEWORK_LIBATBUS_LINK_NAME}
  ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})

add_test(NAME "atgateway.unit_test" COMMAND "$<TARGET_FILE:atgateway-unit_test>")
set_tests_properties("atgateway.unit_test" PROPERTIES LABELS "atgateway;atgateway.unit_test")
