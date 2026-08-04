# =========== RPC unit test support library ===========
# Only added by src/CMakeLists.txt when `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` is true, and only when
# PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS is on.

include("${CMAKE_CURRENT_LIST_DIR}/cmake/ProjectRpcUnitTest.cmake")
# src/CMakeLists.txt adds this directory before including service-functions.cmake, so include it here
# to make project_service_declare_protocol available for the test-only protocol generation.
include("${PROJECT_SOURCE_DIR}/src/service-functions.cmake")

set(PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")

add_library(
  atf4g-co-rpc-unit-test STATIC
  "${CMAKE_CURRENT_LIST_DIR}/src/runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_connector.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_cs.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_db.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_discovery.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_dns.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_router.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_ss.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/raw_transport.cpp"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/runtime.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_connector.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_cs.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_db.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_discovery.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_dns.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_router.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_ss.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/raw_transport.h")
add_library(atf4g-co::rpc-unit-test ALIAS atf4g-co-rpc-unit-test)

target_include_directories(atf4g-co-rpc-unit-test
                           PUBLIC "$<BUILD_INTERFACE:${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}>")
target_compile_definitions(atf4g-co-rpc-unit-test PRIVATE RPC_UNIT_TEST_NATIVE=1)
target_compile_options(atf4g-co-rpc-unit-test PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
target_link_libraries(atf4g-co-rpc-unit-test PUBLIC ${PROJECT_SERVER_FRAME_LIB_LINK}
                                                    ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})

set_target_properties(
  atf4g-co-rpc-unit-test
  PROPERTIES C_VISIBILITY_PRESET "hidden"
             CXX_VISIBILITY_PRESET "hidden"
             INSTALL_RPATH_USE_LINK_PATH YES
             BUILD_WITH_INSTALL_RPATH NO
             BUILD_RPATH_USE_ORIGIN YES)
set_property(TARGET atf4g-co-rpc-unit-test PROPERTY FOLDER "${PROJECT_NAME}/test")

add_subdirectory(test)
