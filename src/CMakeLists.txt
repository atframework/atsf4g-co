set(PROJECT_SOURCE_TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}/templates")
set(PROJECT_SANDBOX_ROOT_DIR "${CMAKE_BINARY_DIR}/_sandbox")
if(EXISTS "${PROJECT_SANDBOX_ROOT_DIR}")
  file(REMOVE_RECURSE "${PROJECT_SANDBOX_ROOT_DIR}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/server_frame/server_frame.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/generate-rpc-task.cmake")

configure_file("${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh.in" "${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh"
               @ONLY NEWLINE_STYLE LF)

set(PROJECT_COMPONENT_PUBLIC_PROTO_PATH "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}"
                                        "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_COMMON_DIR}")
# set(PROJECT_SERVICE_PUBLIC_PROTO_PATH)

add_subdirectory(server_frame)
add_subdirectory(component)

include("${CMAKE_CURRENT_LIST_DIR}/service-functions.cmake")

add_subdirectory(echosvr)
add_subdirectory(authsvr)
add_subdirectory(lobbysvr)

add_subdirectory(tools)
