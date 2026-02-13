get_filename_component(SERVICE_DIRECTORY_NAME ${CMAKE_CURRENT_LIST_DIR} DIRECTORY)
get_filename_component(SERVICE_TARGET_NAME ${SERVICE_DIRECTORY_NAME} NAME_WE)
file(GLOB_RECURSE SERVICE_SOURCES *.cpp *.cc *.c *.cxx)
file(GLOB_RECURSE SERVICE_HEADERS *.h *.hpp)

project_service_declare_instance(
  ${SERVICE_TARGET_NAME}
  "${CMAKE_CURRENT_LIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY
  "${SERVICE_TARGET_NAME}/bin"
  HRADERS
  ${SERVICE_HEADERS}
  SOURCES
  ${SERVICE_SOURCES}
  PRECOMPILE_HEADERS
  "data/player.h"
  "data/player_type_define.h"
  "dispatcher/task_action_cs_req_base.h"
  "rpc/db/hash_table.h"
  RESOURCE_DIRECTORIES
  "${SERVICE_TARGET_NAME}/log"
  USE_SERVICE_SDK
  "lobbysvr-sdk"
  USE_SERVICE_PROTOCOL
  "lobbysvr-config")
