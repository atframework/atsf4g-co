# =========== RPC unit test support library ===========
# Only added by src/CMakeLists.txt when `BUILD_TESTING OR PROJECT_ENABLE_UNITTEST` is true, and only when
# PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS is on.

include("${CMAKE_CURRENT_LIST_DIR}/cmake/ProjectRpcUnitTest.cmake")
# src/CMakeLists.txt adds this directory before including service-functions.cmake, so include it here
# to make project_service_declare_protocol available for the test-only protocol generation.
include("${PROJECT_SOURCE_DIR}/src/service-functions.cmake")

set(PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")

add_library(
  ${PROJECT_NAME}-rpc-unit-test STATIC
  "${CMAKE_CURRENT_LIST_DIR}/src/runtime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_connector.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_cs.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_db.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_discovery.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_dns.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_hpa.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_resource.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_router.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/mock_ss.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/raw_transport.cpp"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/runtime.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_connector.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_cs.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_db.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_discovery.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_dns.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_hpa.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_resource.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_router.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/mock_ss.h"
  "${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}/atframework/testing/raw_transport.h")
add_library(${PROJECT_NAME}::rpc-unit-test ALIAS ${PROJECT_NAME}-rpc-unit-test)

target_include_directories(${PROJECT_NAME}-rpc-unit-test
                           PUBLIC "$<BUILD_INTERFACE:${PROJECT_RPC_UNIT_TEST_PUBLIC_INCLUDE_DIR}>")
target_compile_definitions(${PROJECT_NAME}-rpc-unit-test PRIVATE RPC_UNIT_TEST_NATIVE=1)
# Build-tree default working directory for directly-invoked test executables (no RPC_UNIT_TEST_WORKDIR
# env, no ctest) so they don't write the generated config into the repository root. project_add_rpc_unit_test
# override this per target via RPC_UNIT_TEST_WORKDIR.
target_compile_definitions(${PROJECT_NAME}-rpc-unit-test
PRIVATE "RPC_UNIT_TEST_DEFAULT_WORKDIR=\"${CMAKE_BINARY_DIR}/rpc-unit-test-workdir\"")
# Real excel resource directory so the excel config manager (lazy default file loader) reads the actual
# generated tables instead of requiring mock resource injection. Empty when the resource pipeline did not
# produce the directory yet (the runtime simply skips the bindir entry then).
if(EXISTS "${PROJECT_INSTALL_RES_DIR}/excel")
  target_compile_definitions(${PROJECT_NAME}-rpc-unit-test
  PRIVATE "RPC_UNIT_TEST_EXCEL_RESOURCE_DIR=\"${PROJECT_INSTALL_RES_DIR}/excel\"")
endif()
target_compile_options(${PROJECT_NAME}-rpc-unit-test PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
target_link_libraries(${PROJECT_NAME}-rpc-unit-test PUBLIC ${PROJECT_SERVER_FRAME_LIB_LINK}
                                                    ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME})

set_target_properties(
  ${PROJECT_NAME}-rpc-unit-test
  PROPERTIES C_VISIBILITY_PRESET "hidden"
             CXX_VISIBILITY_PRESET "hidden"
             INSTALL_RPATH_USE_LINK_PATH YES
             BUILD_WITH_INSTALL_RPATH NO
             BUILD_RPATH_USE_ORIGIN YES)
set_property(TARGET ${PROJECT_NAME}-rpc-unit-test PROPERTY FOLDER "${PROJECT_NAME}/test")

add_subdirectory(test)
