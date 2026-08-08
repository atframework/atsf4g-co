set(PROJECT_SOURCE_TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}/templates")
set(PROJECT_SANDBOX_ROOT_DIR "${CMAKE_BINARY_DIR}/_sandbox")
if(EXISTS "${PROJECT_SANDBOX_ROOT_DIR}")
  file(REMOVE_RECURSE "${PROJECT_SANDBOX_ROOT_DIR}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/server_frame/server_frame.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/tools/generate_for_pb_utility.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/component/orbit/sdk/server/generate_for_pb_utility.cmake")

configure_file("${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh.in" "${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh"
               @ONLY NEWLINE_STYLE LF)

set(PROJECT_COMPONENT_PUBLIC_PROTO_PATH "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}"
                                        "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}")
# set(PROJECT_SERVICE_PUBLIC_PROTO_PATH)

add_subdirectory(server_frame)

# RPC unit test support library and CMake helper (project_add_rpc_unit_test). Must be parsed before `component` so
# components can call the helper from their own CMakeLists.txt. CMake functions are not hoisted.
if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(tools/rpc-unit-test)
  # Generated <service>::mock helpers (gated by PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) reference the
  # rpc-unit-test SS rule engine types. Propagate the tool's public headers through the server_frame link
  # interface so every service target linking it inherits the include path.
  target_include_directories(
    ${PROJECT_SERVER_FRAME_LIB_LINK}
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/tools/rpc-unit-test/include>")
  # server_frame component unit tests (cases live next to the component, see doc/docs/development/rpc-unit-test.md).
  add_subdirectory(server_frame/test)
endif()

add_subdirectory(component)

include("${CMAKE_CURRENT_LIST_DIR}/service-functions.cmake")

add_subdirectory(echosvr)
add_subdirectory(cachesvr)
add_subdirectory(authsvr)
add_subdirectory(lobbysvr)
add_subdirectory(rank_settlement_svr)
add_subdirectory(orbitsvr)

add_subdirectory(tools)
add_subdirectory(robot)
