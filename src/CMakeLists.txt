set(PROJECT_SOURCE_TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}/templates")

include("${CMAKE_CURRENT_LIST_DIR}/server_frame/server_frame.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/generate-inserver-rpc-task.cmake")

add_subdirectory(echosvr)

add_subdirectory(server_frame)
add_subdirectory(loginsvr)
add_subdirectory(gamesvr)

add_subdirectory(tools)
