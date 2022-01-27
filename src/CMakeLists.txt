set(PROJECT_SOURCE_TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}/templates")

include("${CMAKE_CURRENT_LIST_DIR}/server_frame/server_frame.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/generate-rpc-task.cmake")

configure_file("${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh.in" "${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh"
               @ONLY NEWLINE_STYLE LF)

add_subdirectory(echosvr)

add_subdirectory(server_frame)
add_subdirectory(component)

add_subdirectory(loginsvr)
add_subdirectory(gamesvr)

add_subdirectory(tools)
