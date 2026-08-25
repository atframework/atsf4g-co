# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

project_game_shared_component_declare_sdk(
  OrbitSDK
  "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_TARGET_NAME
  TARGET_FULL_NAME
  INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/include"
  DLLEXPORT_DECL
  ORBIT_CLIENT_SDK_API
  SHARED_LIBRARY_DECL
  ORBIT_CLIENT_SDK_DLL
  NATIVE_CODE_DECL
  ORBIT_CLIENT_SDK_NATIVE
  USE_COMPONENTS
  "orbit-client-protocol"
  HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitEasyApi.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitConfig.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitClientSdkTypes.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitClientRuntime.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitRPCDispatcher.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitRPCHandle.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/Orbit/OrbitRPCReqBase.h"
  SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/src/Orbit/OrbitEasyApi.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/Orbit/OrbitClientRuntime.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/Orbit/OrbitClientRuntimeRpc.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/Orbit/OrbitRPCDispatcher.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/Orbit/OrbitRPCReqBase.cpp")

set(ORBIT_CLIENT_SDK_TARGET "${TARGET_FULL_NAME}")
target_link_libraries(${ORBIT_CLIENT_SDK_TARGET} PRIVATE tbb)

if(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  target_compile_definitions(${ORBIT_CLIENT_SDK_TARGET} PRIVATE PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS=1)
  if(PROJECT_ENABLE_UNITTEST AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
    add_subdirectory(test)
  endif()
endif()

add_subdirectory(AutoClient)
