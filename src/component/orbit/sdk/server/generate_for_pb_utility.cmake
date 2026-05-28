set(GENERATE_FOR_ORBIT_PB_WORK_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(generate_for_pb_add_orbit_service SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS RPC_IGNORE_EMPTY_REQUEST)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE SERVICE_DLLEXPORT_DECL
                                     RPC_DLLEXPORT_DECL GENERATED_OUTPUT_FILES)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE INCLUDE_HEADERS)
  cmake_parse_arguments(GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}" "${GENERATE_FOR_PB_ARGS_ONE_VALUE}"
                        "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}" ${ARGN})

  if(NOT GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX)
    set(GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX ".")
  endif()

  if(GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX)
    set(HANDLE_PATH_PREFIX "${GENERATE_FOR_PB_ARGS_HANDLE_PATH_PREFIX}/")
  else()
    set(HANDLE_PATH_PREFIX "")
  endif()

  if(NOT GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE)
    set(GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE "")
  endif()

  if(GENERATE_FOR_PB_ARGS_RPC_IGNORE_EMPTY_REQUEST)
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_exclude_request: [ 'google.protobuf.Empty' ]")
  else()
    set(GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST "rpc_exclude_request: [ ]")
  endif()

  if(NOT GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
    string(REGEX REPLACE "[-\\.]" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL "${SERVICE_NAME}")
    string(REGEX REPLACE "[\\\$\\\\/]" "" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(REPLACE "::" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                   "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}_API")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
  endif()

  if(NOT GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
    string(REGEX REPLACE "_API\\$" "" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}_RPC_API" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
  endif()

  if(GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS)
    set(CUSTOM_INCLUDE_HEADERS "include_headers:")
    foreach(INCLUDE_HEADERS ${GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS})
      set(CUSTOM_INCLUDE_HEADERS "${CUSTOM_INCLUDE_HEADERS}
          - '${INCLUDE_HEADERS}'")
    endforeach()
  else()
    set(CUSTOM_INCLUDE_HEADERS "include_headers: [ ]")
  endif()

  set(
    GENERATE_FOR_PB_PRINT_OVERWRITE_RULE_BODY
      "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/handle_orbit_rpc.h.mako'
          output: '${HANDLE_PATH_PREFIX}handle_orbit_rpc_\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/handle_orbit_rpc.cpp.mako'
          output: '${HANDLE_PATH_PREFIX}handle_orbit_rpc_\${service.get_name_lower_rule()}.cpp'
")
  set(
    GENERATE_FOR_PB_PRINT_NON_OVERWRITE_RULE_BODY
      "  # ${SERVICE_NAME} rpc
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
      ${GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST}
      rpc_template:
        - overwrite: false
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/task_action_orbit_rpc.h.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"orbit_action\")}/task_action_\${rpc.get_name()}.h'
        - overwrite: false
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/task_action_orbit_rpc.cpp.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"orbit_action\")}/task_action_\${rpc.get_name()}.cpp'
")
  set(GENERATE_FOR_PB_RULE_BODY
      "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: false
      output_directory: '${SERVICE_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/handle_orbit_rpc.h.mako'
          output: '${HANDLE_PATH_PREFIX}handle_orbit_rpc_\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/handle_orbit_rpc.cpp.mako'
          output: '${HANDLE_PATH_PREFIX}handle_orbit_rpc_\${service.get_name_lower_rule()}.cpp'
      ${GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST}
      rpc_template:
        - overwrite: false
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/task_action_orbit_rpc.h.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"orbit_action\")}/task_action_\${rpc.get_name()}.h'
        - overwrite: false
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/task_action_orbit_rpc.cpp.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"orbit_action\")}/task_action_\${rpc.get_name()}.cpp'
" )
  generate_for_pb_resolve_protocol_codegen_targets(GENERATE_FOR_PB_PROTOCOL_CODEGEN_TARGETS INCLUDE_DEFAULT)
  generate_for_pb_register_flow(
    "${SERVICE_NAME}"
    OUTPUT_VAR_BASE
    "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}"
    RULE_BODY
    "${GENERATE_FOR_PB_RULE_BODY}"
    PRINT_OVERWRITE_RULE_BODY
    "${GENERATE_FOR_PB_PRINT_OVERWRITE_RULE_BODY}"
    PRINT_NON_OVERWRITE_RULE_BODY
    "${GENERATE_FOR_PB_PRINT_NON_OVERWRITE_RULE_BODY}"
    PROTOCOL_CODEGEN_TARGETS
    ${GENERATE_FOR_PB_PROTOCOL_CODEGEN_TARGETS}
    TEMPLATE_DEPENDS
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/handle_orbit_rpc.h.mako"
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/handle_orbit_rpc.cpp.mako"
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/task_action_orbit_rpc.h.mako"
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/task_action_orbit_rpc.cpp.mako")
  if(GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES)
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}")
  else()
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${SERVICE_NAME}")
  endif()
  generate_for_pb_export_output_var_base("${GENERATE_FOR_PB_OUTPUT_VAR_BASE}")
endfunction(generate_for_pb_add_orbit_service)

function(generate_for_pb_add_orbit_client SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE PROJECT_NAMESPACE RPC_ROOT_DIR SERVICE_DLLEXPORT_DECL RPC_DLLEXPORT_DECL GENERATED_OUTPUT_FILES)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE INCLUDE_HEADERS)
  cmake_parse_arguments(GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}" "${GENERATE_FOR_PB_ARGS_ONE_VALUE}"
                        "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}" ${ARGN})
  if(NOT GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE)
    set(GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE "")
  endif()

  if(NOT GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR)
    set(GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR "${SERVICE_ROOT_DIR}")
  endif()

  if(NOT GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
    string(REGEX REPLACE "[-\\.]" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL "${SERVICE_NAME}")
    string(REGEX REPLACE "[\\\$\\\\/]" "" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(REPLACE "::" "_" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL
                   "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}_API")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}" GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL)
  endif()

  if(NOT GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
    string(REGEX REPLACE "_API\\$" "" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL
                         "${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}")
    string(TOUPPER "${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}_RPC_API" GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL)
  endif()

  if(GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS)
    set(CUSTOM_INCLUDE_HEADERS "include_headers:")
    foreach(INCLUDE_HEADERS ${GENERATE_FOR_PB_ARGS_INCLUDE_HEADERS})
      set(CUSTOM_INCLUDE_HEADERS "${CUSTOM_INCLUDE_HEADERS}
          - '${INCLUDE_HEADERS}'")
    endforeach()
  else()
    set(CUSTOM_INCLUDE_HEADERS "include_headers: [ ]")
  endif()

  set(GENERATE_FOR_PB_RULE_BODY
      "  # ${SERVICE_NAME}
  - service:
      name: '${SERVICE_NAME}'
      overwrite: true
      output_directory: '${GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        ${CUSTOM_INCLUDE_HEADERS}
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/rpc_call_api_for_orbit.h.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/rpc_call_api_for_orbit.cpp.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp'
" )
  set(GENERATE_FOR_PB_PRINT_OVERWRITE_RULE_BODY "${GENERATE_FOR_PB_RULE_BODY}")
  generate_for_pb_resolve_protocol_codegen_targets(GENERATE_FOR_PB_PROTOCOL_CODEGEN_TARGETS INCLUDE_DEFAULT)
  generate_for_pb_register_flow(
    "${SERVICE_NAME}"
    OUTPUT_VAR_BASE
    "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}"
    RULE_BODY
    "${GENERATE_FOR_PB_RULE_BODY}"
    PRINT_OVERWRITE_RULE_BODY
    "${GENERATE_FOR_PB_PRINT_OVERWRITE_RULE_BODY}"
    PROTOCOL_CODEGEN_TARGETS
    ${GENERATE_FOR_PB_PROTOCOL_CODEGEN_TARGETS}
    TEMPLATE_DEPENDS
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/rpc_call_api_for_orbit.h.mako"
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/rpc_call_api_for_orbit.cpp.mako")
  if(GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES)
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}")
  else()
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${SERVICE_NAME}")
  endif()
  generate_for_pb_export_output_var_base("${GENERATE_FOR_PB_OUTPUT_VAR_BASE}")
endfunction(generate_for_pb_add_orbit_client)