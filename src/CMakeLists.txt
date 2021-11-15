set(PROJECT_SOURCE_TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}/templates")

include("${CMAKE_CURRENT_LIST_DIR}/server_frame/server_frame.cmake")

set(GENERATE_FOR_PB_PROTO_PATH
  "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/config"
  "${PROJECT_SERVER_FRAME_BAS_DIR}/protocol/pbdesc"
  "${PROJECT_THIRD_PARTY_INSTALL_DIR}/include"
  "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
  "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include"
)
unset(GENERATE_FOR_PB_EXTEND_PROTO_FILES)
if (PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR AND EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto")
  list(APPEND GENERATE_FOR_PB_PROTO_PATH "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}")
  list(APPEND GENERATE_FOR_PB_EXTEND_PROTO_FILES "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto"
    "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader_ue.proto")
endif()
if (PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR AND EXISTS "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto")
  list(APPEND GENERATE_FOR_PB_PROTO_PATH "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension")
  list(APPEND GENERATE_FOR_PB_EXTEND_PROTO_FILES "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension/xrescode_extensions_v3.proto")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/generate-rpc-task.cmake")

configure_file("${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh.in" "${CMAKE_CURRENT_LIST_DIR}/generate-nomsg-task.sh"
               @ONLY NEWLINE_STYLE LF)

add_subdirectory(echosvr)

add_subdirectory(server_frame)
add_subdirectory(loginsvr)
add_subdirectory(gamesvr)

add_subdirectory(tools)
