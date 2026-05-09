project_game_shared_component_declare_sdk(
  OrbitSDK
  "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_TARGET_NAME
  TARGET_FULL_NAME
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/include"
  DLLEXPORT_DECL
  ORBIT_CLIENT_SDK_API
  USE_COMPONENTS
  "orbit-client-protocol"
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitConfig.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitClientSdkTypes.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitClientRuntime.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/src/Orbit/OrbitClientRuntime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/Orbit/OrbitClientRuntimeRpc.cpp")

set(ORBIT_CLIENT_SDK_TARGET "${TARGET_FULL_NAME}")
add_subdirectory(AutoClient)