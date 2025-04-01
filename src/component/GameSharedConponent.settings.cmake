set(PROJECT_GAME_SHARED_COMPONENT_PRIVATE_DEFINITIONS GAME_SHARED_COMPONENT_SERVER_MODE=1
                                                      "PROJECT_PROTOCOL_NAMESPACE_ID=${PROJECT_NAMESPACE}")

function(project_game_shared_component_declare_sdk TARGET_NAME SDK_ROOT_DIR)
  set(optionArgs "STATIC;SHARED")
  set(oneValueArgs INCLUDE_DIR OUTPUT_NAME OUTPUT_TARGET_NAME DLLEXPORT_DECL SHARED_LIBRARY_DECL NATIVE_CODE_DECL)
  set(multiValueArgs HRADERS SOURCES USE_COMPONENTS)
  cmake_parse_arguments(__declare_sdk "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(__append_args)
  if(NOT __declare_sdk_OUTPUT_TARGET_NAME)
    set(__append_args OUTPUT_TARGET_NAME __output_target_name)
    set(__game_shared_component_name_var __output_target_name)
  else()
    set(__game_shared_component_name_var ${__declare_sdk_OUTPUT_TARGET_NAME})
  endif()
  project_component_declare_sdk("${TARGET_NAME}" "${SDK_ROOT_DIR}" ${__append_args} ${ARGN})

  target_compile_definitions("${${__game_shared_component_name_var}}"
                             PRIVATE ${PROJECT_GAME_SHARED_COMPONENT_PRIVATE_DEFINITIONS})
  if(__declare_sdk_OUTPUT_TARGET_NAME)
    set(${__declare_sdk_OUTPUT_TARGET_NAME}
        "${TARGET_FULL_NAME}"
        PARENT_SCOPE)
  endif()
endfunction()
