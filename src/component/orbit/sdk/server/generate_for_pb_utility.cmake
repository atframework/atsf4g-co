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
  generate_for_pb_register_flow(
    "${SERVICE_NAME}"
    OUTPUT_VAR_BASE
    "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}"
    RULE_BODY
    "${GENERATE_FOR_PB_RULE_BODY}"
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
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES}" PARENT_SCOPE)
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES}" PARENT_SCOPE)
endfunction(generate_for_pb_add_orbit_service)

function(generate_for_pb_add_orbit_client SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE PROJECT_NAMESPACE RPC_ROOT_DIR SERVICE_DLLEXPORT_DECL RPC_DLLEXPORT_DECL GENERATED_OUTPUT_FILES)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE INCLUDE_HEADERS)
  cmake_parse_arguments(GENERATE_FOR_PB_ARGS "${GENERATE_FOR_PB_ARGS_OPTIONS}" "${GENERATE_FOR_PB_ARGS_ONE_VALUE}"
                        "${GENERATE_FOR_PB_ARGS_MULTI_VALUE}" ${ARGN})
  # TODO 生成代码写入 GENERATED_OUTPUT_FILES 列表 统一添加变量后缀
  # TODO 生成PB文件 generate_for_pb_add_proto_pb_file
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
  generate_for_pb_register_flow(
    "${SERVICE_NAME}"
    OUTPUT_VAR_BASE
    "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}"
    RULE_BODY
    "${GENERATE_FOR_PB_RULE_BODY}"
    TEMPLATE_DEPENDS
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/rpc_call_api_for_orbit.h.mako"
    "${GENERATE_FOR_ORBIT_PB_WORK_DIR}/template/rpc_call_api_for_orbit.cpp.mako")
  if(GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES)
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}")
  else()
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${SERVICE_NAME}")
  endif()
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES}" PARENT_SCOPE)
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES}" PARENT_SCOPE)
endfunction(generate_for_pb_add_orbit_client)