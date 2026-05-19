set(GENERATE_FOR_PB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(GENERATE_FOR_PB_MAKO_PY "${GENERATE_FOR_PB_SOURCE_DIR}/tools/generate-for-pb/mako-generator.py")
set(GENERATE_FOR_PB_JINJA2_PY "${GENERATE_FOR_PB_SOURCE_DIR}/tools/generate-for-pb/jinja2-generator.py")
set(GENERATE_FOR_PB_PY "${GENERATE_FOR_PB_MAKO_PY}")
set(GENERATE_FOR_PB_WORK_DIR "${CMAKE_BINARY_DIR}")
set(GENERATE_FOR_PB_OUT_SH "${CMAKE_BINARY_DIR}/generate-for-pb-run.sh")
set(GENERATE_FOR_PB_OUT_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-run.ps1")
set(GENERATE_FOR_PB_PROTO_SH "${CMAKE_BINARY_DIR}/generate-for-pb-generate-pb.sh")
set(GENERATE_FOR_PB_PROTO_PWSH "${CMAKE_BINARY_DIR}/generate-for-pb-generate-pb.ps1")
set(GENERATE_FOR_PB_OUT_CONF "${CMAKE_BINARY_DIR}/generate-for-pb-run.yaml")
set(GENERATE_FOR_PB_OUT_LOG "${CMAKE_BINARY_DIR}/generate-for-pb-run.log")
set(GENERATE_FOR_PB_OUT_PB "${PROJECT_GENERATED_PBD_DIR}/network.pb")
unset(GENERATE_FOR_PB_PROTO_COMMAND)
file(WRITE "${GENERATE_FOR_PB_OUT_LOG}" "# generate-for-pb-run")
if(NOT PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR)
  set(PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR "${PROJECT_THIRD_PARTY_INSTALL_DIR}/.python_modules")
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.11.0")
  set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF-8")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL "3.8.0")
  set(GENERATE_FOR_PB_PY_ENCODING ENCODING "UTF8")
else()
  unset(GENERATE_FOR_PB_PY_ENCODING)
endif()
# Python3_EXECUTABLE
if(NOT Python3_EXECUTABLE)
  find_package(Python3 COMPONENTS Interpreter)
  if(NOT Python_Interpreter_FOUND)
    message(FATAL_ERROR "python3/python is required.")
  endif()
endif()

set(GENERATE_FOR_PB_PROROC_BIN ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC})
if(NOT GENERATE_FOR_PB_PROROC_BIN)
  message(FATAL_ERROR "Protobuf - protoc is required.")
endif()

file(
  WRITE "${GENERATE_FOR_PB_OUT_CONF}"
  "configure:
  encoding: 'UTF-8'
  output_directory: 'output directory'
  overwrite: false
  paths:
    - \"${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/modules\"")
foreach(GENERATE_FOR_PB_ARGS_ADD_PATH GENERATE_FOR_PB_MODULE_PROTOBUF_PYTHON_PATH
                                      GENERATE_FOR_PB_MODULE_MAKO_PYTHON_PATH GENERATE_FOR_PB_MODULE_SIX_PYTHON_PATH)
  if(${GENERATE_FOR_PB_ARGS_ADD_PATH})
    file(APPEND "${GENERATE_FOR_PB_OUT_CONF}" "
      - \"${${GENERATE_FOR_PB_ARGS_ADD_PATH}}\"")
  endif()
endforeach()
file(
  APPEND "${GENERATE_FOR_PB_OUT_CONF}"
  "
  package_prefix:
    - \"${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}\"
  protocol_project_directory: '${PROJECT_SOURCE_DIR}'
  protocol_input_pb_file: '${GENERATE_FOR_PB_OUT_PB}'

rules:
  # Rules to generate rpc codes
")

set(GENERATE_FOR_PB_TARGET "generate-for-pb")
set(GENERATE_FOR_PB_CONF_ROOT_DIR "${CMAKE_BINARY_DIR}/_generated/generate-for-pb")
set(GENERATE_FOR_PB_CONF_DIR "${GENERATE_FOR_PB_CONF_ROOT_DIR}/conf")
file(MAKE_DIRECTORY "${GENERATE_FOR_PB_CONF_DIR}")

if(NOT TARGET ${GENERATE_FOR_PB_TARGET})
  add_custom_target(${GENERATE_FOR_PB_TARGET})
  set_property(TARGET ${GENERATE_FOR_PB_TARGET} PROPERTY FOLDER "${PROJECT_NAME}/codegen")
endif()

function(generate_for_pb_append_global_list PROPERTY_NAME)
  get_property(_generate_for_pb_current_list GLOBAL PROPERTY "${PROPERTY_NAME}")
  if(NOT _generate_for_pb_current_list)
    set(_generate_for_pb_current_list)
  endif()

  foreach(_generate_for_pb_item IN LISTS ARGN)
    if(_generate_for_pb_item)
      list(APPEND _generate_for_pb_current_list "${_generate_for_pb_item}")
    endif()
  endforeach()

  if(_generate_for_pb_current_list)
    list(REMOVE_DUPLICATES _generate_for_pb_current_list)
  endif()

  set_property(GLOBAL PROPERTY "${PROPERTY_NAME}" "${_generate_for_pb_current_list}")
endfunction()

function(generate_for_pb_get_global_list PROPERTY_NAME OUTPUT_VAR)
  get_property(_generate_for_pb_current_list GLOBAL PROPERTY "${PROPERTY_NAME}")
  if(NOT _generate_for_pb_current_list)
    set(_generate_for_pb_current_list)
  endif()

  set(${OUTPUT_VAR} "${_generate_for_pb_current_list}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_make_property_name PREFIX RAW_NAME OUTPUT_VAR)
  string(MAKE_C_IDENTIFIER "${RAW_NAME}" _generate_for_pb_property_suffix)
  set(${OUTPUT_VAR} "${PREFIX}_${_generate_for_pb_property_suffix}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_register_protocol_pb_file TARGET_NAME PB_FILE)
  generate_for_pb_make_property_name("GENERATE_FOR_PB_PROTOCOL_FILE" "${TARGET_NAME}" _generate_for_pb_property_name)
  set_property(GLOBAL PROPERTY "${_generate_for_pb_property_name}" "${PB_FILE}")
endfunction()

function(generate_for_pb_register_protocol_codegen_target TARGET_NAME)
  if(TARGET_NAME)
    generate_for_pb_append_global_list("GENERATE_FOR_PB_PROTOCOL_CODEGEN_TARGETS" "${TARGET_NAME}")
  endif()
endfunction()

function(generate_for_pb_resolve_protocol_pb_file TARGET_NAME OUTPUT_VAR)
  generate_for_pb_make_property_name("GENERATE_FOR_PB_PROTOCOL_FILE" "${TARGET_NAME}" _generate_for_pb_property_name)
  get_property(_generate_for_pb_pb_file GLOBAL PROPERTY "${_generate_for_pb_property_name}")
  set(${OUTPUT_VAR} "${_generate_for_pb_pb_file}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_register_protocol_inputs PROTO_ROOT_DIR)
  if(PROTO_ROOT_DIR)
    generate_for_pb_append_global_list("GENERATE_FOR_PB_PROTO_PATHS" "${PROTO_ROOT_DIR}")
  endif()
  if(ARGN)
    generate_for_pb_append_global_list("GENERATE_FOR_PB_PROTO_FILES" ${ARGN})
  endif()
endfunction()

function(generate_for_pb_resolve_external_pb_inputs OUTPUT_VAR)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs RAW_PB_FILES COMPONENT_PROTOCOLS SERVICE_PROTOCOLS)
  cmake_parse_arguments(GENERATE_FOR_PB_EXTERNAL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(_generate_for_pb_resolved_pb_files)
  foreach(_generate_for_pb_candidate IN LISTS GENERATE_FOR_PB_EXTERNAL_RAW_PB_FILES)
    if(NOT _generate_for_pb_candidate)
      continue()
    endif()

    unset(_generate_for_pb_resolved_pb_file)
    if(EXISTS "${_generate_for_pb_candidate}")
      get_filename_component(_generate_for_pb_resolved_pb_file "${_generate_for_pb_candidate}" ABSOLUTE BASE_DIR
                             "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
      generate_for_pb_resolve_protocol_pb_file("${_generate_for_pb_candidate}" _generate_for_pb_resolved_pb_file)
      if(NOT _generate_for_pb_resolved_pb_file)
        get_filename_component(_generate_for_pb_resolved_pb_file "${_generate_for_pb_candidate}" ABSOLUTE BASE_DIR
                               "${CMAKE_CURRENT_SOURCE_DIR}")
      endif()
    endif()

    if(_generate_for_pb_resolved_pb_file)
      list(APPEND _generate_for_pb_resolved_pb_files "${_generate_for_pb_resolved_pb_file}")
    endif()
  endforeach()

  foreach(_generate_for_pb_protocol_name IN LISTS GENERATE_FOR_PB_EXTERNAL_COMPONENT_PROTOCOLS
                                                  GENERATE_FOR_PB_EXTERNAL_SERVICE_PROTOCOLS)
    if(NOT _generate_for_pb_protocol_name)
      continue()
    endif()

    unset(_generate_for_pb_resolved_pb_file)
    generate_for_pb_resolve_protocol_pb_file("${_generate_for_pb_protocol_name}" _generate_for_pb_resolved_pb_file)
    if(NOT _generate_for_pb_resolved_pb_file)
      message(FATAL_ERROR
              "Can not resolve descriptor PB file for protocol ${_generate_for_pb_protocol_name} in generate-for-pb.")
    endif()
    list(APPEND _generate_for_pb_resolved_pb_files "${_generate_for_pb_resolved_pb_file}")
  endforeach()

  if(_generate_for_pb_resolved_pb_files)
    list(REMOVE_DUPLICATES _generate_for_pb_resolved_pb_files)
  endif()
  set(${OUTPUT_VAR} "${_generate_for_pb_resolved_pb_files}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_build_conf_header OUTPUT_VAR)
  set(options WITHOUT_EXTERNAL_PB_FILES)
  cmake_parse_arguments(GENERATE_FOR_PB_HEADER "${options}" "" "" ${ARGN})

  set(
    _generate_for_pb_header
    "configure:
  encoding: 'UTF-8'
  output_directory: 'output directory'
  overwrite: false
  paths:
    - \"${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/modules\"")
  foreach(GENERATE_FOR_PB_ARGS_ADD_PATH GENERATE_FOR_PB_MODULE_PROTOBUF_PYTHON_PATH
                                        GENERATE_FOR_PB_MODULE_MAKO_PYTHON_PATH GENERATE_FOR_PB_MODULE_SIX_PYTHON_PATH)
    if(${GENERATE_FOR_PB_ARGS_ADD_PATH})
      string(APPEND _generate_for_pb_header "
    - \"${${GENERATE_FOR_PB_ARGS_ADD_PATH}}\"")
    endif()
  endforeach()

  string(
    APPEND _generate_for_pb_header
    "
  package_prefix:
    - \"${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}\"
  protocol_project_directory: '${PROJECT_SOURCE_DIR}'
  protocol_input_pb_file: '${GENERATE_FOR_PB_OUT_PB}'")

  if(NOT GENERATE_FOR_PB_HEADER_WITHOUT_EXTERNAL_PB_FILES)
    generate_for_pb_get_global_list("GENERATE_FOR_PB_EXTERNAL_PB_FILES" _generate_for_pb_external_pb_files)
  endif()
  if(_generate_for_pb_external_pb_files)
    string(APPEND _generate_for_pb_header "
  protocol_external_pb_files:")
    foreach(_generate_for_pb_external_pb_file IN LISTS _generate_for_pb_external_pb_files)
      string(APPEND _generate_for_pb_header "
    - '${_generate_for_pb_external_pb_file}'")
    endforeach()
  else()
    string(APPEND _generate_for_pb_header "
  protocol_external_pb_files: [ ]")
  endif()

  string(APPEND _generate_for_pb_header "

rules:
  # Rules to generate rpc codes
")
  set(${OUTPUT_VAR} "${_generate_for_pb_header}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_refresh_conf_files)
  generate_for_pb_build_conf_header(_generate_for_pb_header)
  generate_for_pb_get_global_list("GENERATE_FOR_PB_FLOW_IDS" _generate_for_pb_flow_ids)
  foreach(_generate_for_pb_flow_id IN LISTS _generate_for_pb_flow_ids)
    get_property(_generate_for_pb_rule_file GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_RULE_FILE_${_generate_for_pb_flow_id}")
    get_property(_generate_for_pb_conf_file GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_CONF_FILE_${_generate_for_pb_flow_id}")
    if(NOT _generate_for_pb_rule_file OR NOT _generate_for_pb_conf_file)
      continue()
    endif()

    file(READ "${_generate_for_pb_rule_file}" _generate_for_pb_rule_body)
    file(WRITE "${_generate_for_pb_conf_file}" "${_generate_for_pb_header}${_generate_for_pb_rule_body}")
  endforeach()
endfunction()

function(generate_for_pb_get_builtin_proto_files OUTPUT_VAR)
  file(GLOB _generate_for_pb_builtin_proto_files "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include/*.proto"
       "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include/atframe/*.proto")
  list(APPEND _generate_for_pb_builtin_proto_files
       "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/any.proto"
       "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/empty.proto"
       "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/duration.proto"
       "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/timestamp.proto"
       "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}/google/protobuf/descriptor.proto")
  list(REMOVE_DUPLICATES _generate_for_pb_builtin_proto_files)
  set(${OUTPUT_VAR} "${_generate_for_pb_builtin_proto_files}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_collect_proto_inputs OUTPUT_PATHS OUTPUT_FILES)
  generate_for_pb_get_global_list("GENERATE_FOR_PB_PROTO_PATHS" _generate_for_pb_proto_paths)
  generate_for_pb_get_global_list("GENERATE_FOR_PB_PROTO_FILES" _generate_for_pb_proto_files)

  if(PROJECT_COMPONENT_PUBLIC_PROTO_PATH)
    list(APPEND _generate_for_pb_proto_paths ${PROJECT_COMPONENT_PUBLIC_PROTO_PATH})
  endif()
  if(PROJECT_SERVICE_PUBLIC_PROTO_PATH)
    list(APPEND _generate_for_pb_proto_paths ${PROJECT_SERVICE_PUBLIC_PROTO_PATH})
  endif()

  if(PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR)
    list(APPEND _generate_for_pb_proto_paths "${PROJECT_SERVER_FRAME_PROTO_SANDBOX_EXTENSION_DIR}")
  elseif(EXISTS "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb")
    list(APPEND _generate_for_pb_proto_paths "${CMAKE_BINARY_DIR}/_sandbox/generate-for-pb")
  endif()
  if(PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR)
    list(APPEND _generate_for_pb_proto_paths "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}")
  endif()
  if(PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR)
    list(APPEND _generate_for_pb_proto_paths "${PROJECT_THIRD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/pb_extension")
  endif()

  if(_generate_for_pb_proto_paths)
    list(REMOVE_DUPLICATES _generate_for_pb_proto_paths)
  endif()
  if(_generate_for_pb_proto_files)
    list(REMOVE_DUPLICATES _generate_for_pb_proto_files)
  endif()

  set(${OUTPUT_PATHS} "${_generate_for_pb_proto_paths}" PARENT_SCOPE)
  set(${OUTPUT_FILES} "${_generate_for_pb_proto_files}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_prepare_input_pb)
  generate_for_pb_collect_proto_inputs(_generate_for_pb_proto_paths _generate_for_pb_proto_files)
  if(NOT _generate_for_pb_proto_files)
    message(FATAL_ERROR "No protocol files were registered for generate-for-pb.")
  endif()

  generate_for_pb_get_builtin_proto_files(_generate_for_pb_builtin_proto_files)
  set(_generate_for_pb_protoc_args "${GENERATE_FOR_PB_PROROC_BIN}" -o "${GENERATE_FOR_PB_OUT_PB}" --proto_path
                                   "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}" --proto_path
                                   "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include" --proto_path
                                   "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include")
  foreach(_generate_for_pb_proto_path IN LISTS _generate_for_pb_proto_paths)
    list(APPEND _generate_for_pb_protoc_args --proto_path "${_generate_for_pb_proto_path}")
  endforeach()
  list(APPEND _generate_for_pb_protoc_args ${_generate_for_pb_proto_files} ${_generate_for_pb_builtin_proto_files})

  file(MAKE_DIRECTORY "${PROJECT_GENERATED_PBD_DIR}")
  execute_process(
    COMMAND ${_generate_for_pb_protoc_args}
    RESULT_VARIABLE _generate_for_pb_protoc_result
    WORKING_DIRECTORY "${GENERATE_FOR_PB_WORK_DIR}"
    OUTPUT_FILE "${GENERATE_FOR_PB_OUT_LOG}"
    ERROR_FILE "${GENERATE_FOR_PB_OUT_LOG}" COMMAND_ECHO STDOUT ${GENERATE_FOR_PB_PY_ENCODING})
  if(NOT _generate_for_pb_protoc_result EQUAL 0)
    message(FATAL_ERROR "Generate ${GENERATE_FOR_PB_OUT_PB} failed. See ${GENERATE_FOR_PB_OUT_LOG} for details.")
  endif()
endfunction()

function(generate_for_pb_print_output_files FLOW_ID OUTPUT_VAR)
  get_property(_generate_for_pb_rule_file GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_RULE_FILE_${FLOW_ID}")
  if(NOT _generate_for_pb_rule_file)
    set(${OUTPUT_VAR} "" PARENT_SCOPE)
    return()
  endif()

  generate_for_pb_build_conf_header(_generate_for_pb_header WITHOUT_EXTERNAL_PB_FILES)
  file(READ "${_generate_for_pb_rule_file}" _generate_for_pb_rule_body)
  set(_generate_for_pb_print_conf_file "${GENERATE_FOR_PB_CONF_DIR}/${FLOW_ID}.print.yaml")
  file(WRITE "${_generate_for_pb_print_conf_file}" "${_generate_for_pb_header}${_generate_for_pb_rule_body}")

  generate_for_pb_prepare_input_pb()
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${GENERATE_FOR_PB_PY}" --add-package-prefix
            "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}" --clang-format-path "${PROJECT_TOOL_CLANG_FORMAT}"
            --print-output-files --quiet -c "${_generate_for_pb_print_conf_file}"
    RESULT_VARIABLE _generate_for_pb_print_result
    WORKING_DIRECTORY "${GENERATE_FOR_PB_WORK_DIR}"
    OUTPUT_VARIABLE _generate_for_pb_print_stdout
    ERROR_VARIABLE _generate_for_pb_print_stderr ${GENERATE_FOR_PB_PY_ENCODING})
  if(NOT _generate_for_pb_print_result EQUAL 0)
    message(
      FATAL_ERROR
        "Print output files for ${FLOW_ID} failed: ${_generate_for_pb_print_stderr}${_generate_for_pb_print_stdout}")
  endif()

  string(REGEX MATCHALL "[^\r\n]+" _generate_for_pb_print_files "${_generate_for_pb_print_stdout}")
  set(${OUTPUT_VAR} "${_generate_for_pb_print_files}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_register_flow FLOW_NAME)
  set(options "")
  set(oneValueArgs OUTPUT_VAR_BASE RULE_BODY)
  set(multiValueArgs TEMPLATE_DEPENDS EXTERNAL_PB_FILES)
  cmake_parse_arguments(GENERATE_FOR_PB_FLOW "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT GENERATE_FOR_PB_FLOW_RULE_BODY)
    return()
  endif()
  if(NOT DEFINED GENERATE_FOR_PB_FLOW_OUTPUT_VAR_BASE OR NOT GENERATE_FOR_PB_FLOW_OUTPUT_VAR_BASE)
    set(GENERATE_FOR_PB_FLOW_OUTPUT_VAR_BASE "${FLOW_NAME}")
  endif()

  if(GENERATE_FOR_PB_FLOW_EXTERNAL_PB_FILES)
    generate_for_pb_add_proto_pb_file(${GENERATE_FOR_PB_FLOW_EXTERNAL_PB_FILES})
  endif()

  get_property(_generate_for_pb_flow_index GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_INDEX")
  if(NOT _generate_for_pb_flow_index)
    set(_generate_for_pb_flow_index 0)
  endif()
  math(EXPR _generate_for_pb_flow_index "${_generate_for_pb_flow_index} + 1")
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_INDEX" "${_generate_for_pb_flow_index}")

  string(MAKE_C_IDENTIFIER "${GENERATE_FOR_PB_FLOW_OUTPUT_VAR_BASE}_${_generate_for_pb_flow_index}"
         _generate_for_pb_flow_id)
  set(_generate_for_pb_rule_file "${GENERATE_FOR_PB_CONF_DIR}/${_generate_for_pb_flow_id}.rule.yaml")
  set(_generate_for_pb_conf_file "${GENERATE_FOR_PB_CONF_DIR}/${_generate_for_pb_flow_id}.yaml")
  set(_generate_for_pb_target_name "${GENERATE_FOR_PB_TARGET}-${_generate_for_pb_flow_id}")
  file(WRITE "${_generate_for_pb_rule_file}" "${GENERATE_FOR_PB_FLOW_RULE_BODY}")

  generate_for_pb_append_global_list("GENERATE_FOR_PB_FLOW_IDS" "${_generate_for_pb_flow_id}")
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_NAME_${_generate_for_pb_flow_id}" "${FLOW_NAME}")
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_RULE_FILE_${_generate_for_pb_flow_id}"
               "${_generate_for_pb_rule_file}")
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_CONF_FILE_${_generate_for_pb_flow_id}"
               "${_generate_for_pb_conf_file}")
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_TARGET_${_generate_for_pb_flow_id}"
               "${_generate_for_pb_target_name}")
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_TEMPLATE_DEPENDS_${_generate_for_pb_flow_id}"
               "${GENERATE_FOR_PB_FLOW_TEMPLATE_DEPENDS}")

  generate_for_pb_refresh_conf_files()
  generate_for_pb_print_output_files("${_generate_for_pb_flow_id}" _generate_for_pb_all_outputs)
  if(_generate_for_pb_all_outputs)
    list(REMOVE_DUPLICATES _generate_for_pb_all_outputs)
    set_source_files_properties(${_generate_for_pb_all_outputs} PROPERTIES GENERATED TRUE)
  endif()
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_OUTPUTS_${_generate_for_pb_flow_id}"
               "${_generate_for_pb_all_outputs}")

  set(_generate_for_pb_source_outputs)
  set(_generate_for_pb_header_outputs)
  foreach(_generate_for_pb_output_file IN LISTS _generate_for_pb_all_outputs)
    if(_generate_for_pb_output_file MATCHES "\\.(c|cc|cpp|cxx)$")
      list(APPEND _generate_for_pb_source_outputs "${_generate_for_pb_output_file}")
    elseif(_generate_for_pb_output_file MATCHES "\\.(h|hh|hpp|hxx)$")
      list(APPEND _generate_for_pb_header_outputs "${_generate_for_pb_output_file}")
    endif()
  endforeach()
  if(_generate_for_pb_source_outputs)
    list(REMOVE_DUPLICATES _generate_for_pb_source_outputs)
  endif()
  if(_generate_for_pb_header_outputs)
    list(REMOVE_DUPLICATES _generate_for_pb_header_outputs)
  endif()

  if(GENERATE_FOR_PB_FLOW_OUTPUT_VAR_BASE)
    set(${GENERATE_FOR_PB_FLOW_OUTPUT_VAR_BASE}_SOURCE_FILES "${_generate_for_pb_source_outputs}" PARENT_SCOPE)
    set(${GENERATE_FOR_PB_FLOW_OUTPUT_VAR_BASE}_HEADER_FILES "${_generate_for_pb_header_outputs}" PARENT_SCOPE)
  endif()
endfunction()

macro(generate_output_file_add_to_source_list DEST GENERATED_OUTPUT_FILES)
  set(_generate_for_pb_source_var "${GENERATED_OUTPUT_FILES}_SOURCE_FILES")
  if(DEFINED ${_generate_for_pb_source_var} AND ${_generate_for_pb_source_var})
    list(APPEND ${DEST} ${${_generate_for_pb_source_var}})
    set_source_files_properties(${${_generate_for_pb_source_var}} PROPERTIES GENERATED TRUE)
  endif()
endmacro()

macro(generate_output_file_add_to_header_list DEST GENERATED_OUTPUT_FILES)
  set(_generate_for_pb_header_var "${GENERATED_OUTPUT_FILES}_HEADER_FILES")
  if(DEFINED ${_generate_for_pb_header_var} AND ${_generate_for_pb_header_var})
    list(APPEND ${DEST} ${${_generate_for_pb_header_var}})
    set_source_files_properties(${${_generate_for_pb_header_var}} PROPERTIES GENERATED TRUE)
  endif()
endmacro()

macro(generate_output_file_add_to_list DEST GENERATED_OUTPUT_FILES)
  generate_output_file_add_to_source_list(${DEST} ${GENERATED_OUTPUT_FILES})
  generate_output_file_add_to_header_list(${DEST} ${GENERATED_OUTPUT_FILES})
endmacro()

function(generate_for_pb_collect_filtered_output_files ROOT_DIR OUTPUT_BASES SOURCE_OUTPUT_VAR HEADER_OUTPUT_VAR)
  get_filename_component(_generate_for_pb_root_dir "${ROOT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(_generate_for_pb_filtered_source_files)
  set(_generate_for_pb_filtered_header_files)

  foreach(_generate_for_pb_output_base IN LISTS OUTPUT_BASES)
    if(NOT _generate_for_pb_output_base)
      continue()
    endif()

    foreach(_generate_for_pb_output_kind SOURCE HEADER)
      set(_generate_for_pb_output_var "${_generate_for_pb_output_base}_${_generate_for_pb_output_kind}_FILES")
      if(NOT DEFINED ${_generate_for_pb_output_var})
        continue()
      endif()

      foreach(_generate_for_pb_output_file IN LISTS ${_generate_for_pb_output_var})
        if(NOT _generate_for_pb_output_file)
          continue()
        endif()

        if(IS_ABSOLUTE "${_generate_for_pb_output_file}")
          set(_generate_for_pb_output_file_abs "${_generate_for_pb_output_file}")
        else()
          get_filename_component(_generate_for_pb_output_file_abs "${_generate_for_pb_output_file}" ABSOLUTE
                                 BASE_DIR "${_generate_for_pb_root_dir}")
        endif()

        file(RELATIVE_PATH _generate_for_pb_relative_path "${_generate_for_pb_root_dir}"
             "${_generate_for_pb_output_file_abs}")
        if(_generate_for_pb_relative_path MATCHES "^\\.\\.([/\\\\]|$)")
          continue()
        endif()

        if(_generate_for_pb_output_kind STREQUAL "SOURCE")
          list(APPEND _generate_for_pb_filtered_source_files "${_generate_for_pb_output_file_abs}")
        else()
          list(APPEND _generate_for_pb_filtered_header_files "${_generate_for_pb_output_file_abs}")
        endif()
      endforeach()
    endforeach()
  endforeach()

  if(_generate_for_pb_filtered_source_files)
    list(REMOVE_DUPLICATES _generate_for_pb_filtered_source_files)
  endif()
  if(_generate_for_pb_filtered_header_files)
    list(REMOVE_DUPLICATES _generate_for_pb_filtered_header_files)
  endif()

  set(${SOURCE_OUTPUT_VAR} "${_generate_for_pb_filtered_source_files}" PARENT_SCOPE)
  set(${HEADER_OUTPUT_VAR} "${_generate_for_pb_filtered_header_files}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_add_proto_pb_file)
  foreach(PROTO_PB_FILE ${ARGN})
    if(NOT PROTO_PB_FILE)
      continue()
    endif()
    if(NOT IS_ABSOLUTE "${PROTO_PB_FILE}")
      get_filename_component(PROTO_PB_FILE "${PROTO_PB_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    generate_for_pb_append_global_list("GENERATE_FOR_PB_EXTERNAL_PB_FILES" "${PROTO_PB_FILE}")
  endforeach()
  generate_for_pb_refresh_conf_files()
endfunction()

function(generate_for_pb_add_custom_configure YAML_INPUT)
  string(REGEX MATCHALL "[^\r\n]+" YAML_LINES "${YAML_INPUT}")
  if(NOT YAML_LINES)
    return()
  endif()

  set(STRIP_FIRST_LINES TRUE)
  set(COMMON_SPACE_PREFIX_LENGTH)
  foreach(YAML_LINE IN LISTS YAML_LINES)
    if(STRIP_FIRST_LINES)
      if(YAML_LINE MATCHES "^[ \\t]*$")
        continue()
      else()
        set(STRIP_FIRST_LINES FALSE)
        unset(YAML_LINE_PREFIX)
        if(YAML_LINE AND YAML_LINE MATCHES "^[ \\t]*")
          string(LENGTH "${CMAKE_MATCH_1}" COMMON_SPACE_PREFIX_LENGTH)
        else()
          set(COMMON_SPACE_PREFIX_LENGTH 0)
        endif()
      endif()
    else()
      unset(YAML_LINE_PREFIX)
      if(YAML_LINE AND YAML_LINE MATCHES "^[ \\t]*")
        string(LENGTH "${CMAKE_MATCH_1}" YAML_LINE_SPACE_PREFIX_LENGTH)
        if(YAML_LINE_SPACE_PREFIX_LENGTH LESS COMMON_SPACE_PREFIX_LENGTH)
          set(COMMON_SPACE_PREFIX_LENGTH ${YAML_LINE_SPACE_PREFIX_LENGTH})
        endif()
      else()
        set(COMMON_SPACE_PREFIX_LENGTH 0)
      endif()
    endif()
    if(COMMON_SPACE_PREFIX_LENGTH LESS_EQUAL 0)
      break()
    endif()
  endforeach()

  unset(YAML_OUTPUT)
  unset(YAML_OUTPUT_CACHE)
  set(STRIP_FIRST_LINES TRUE)
  foreach(YAML_LINE IN LISTS YAML_LINES)
    if(STRIP_FIRST_LINES)
      if(NOT YAML_LINE OR YAML_LINE MATCHES "^[ \\t]*$")
        continue()
      else()
        set(STRIP_FIRST_LINES FALSE)
      endif()
    endif()
    if(COMMON_SPACE_PREFIX_LENGTH GREATER 0)
      string(SUBSTRING "${YAML_LINE}" ${COMMON_SPACE_PREFIX_LENGTH} -1 YAML_LINE)
    endif()
    if(YAML_LINE MATCHES "^[ \\t]*$")
      string(APPEND YAML_OUTPUT_CACHE "  ${YAML_LINE}\n")
    else()
      if(YAML_OUTPUT_CACHE)
        string(APPEND YAML_OUTPUT "${YAML_OUTPUT_CACHE}")
        unset(YAML_OUTPUT_CACHE)
      endif()
      string(APPEND YAML_OUTPUT "  ${YAML_LINE}\n")
    endif()
  endforeach()

  if(YAML_OUTPUT_CACHE)
    string(APPEND YAML_OUTPUT "${YAML_OUTPUT_CACHE}")
    unset(YAML_OUTPUT_CACHE)
  endif()

  generate_for_pb_register_flow("custom-configure" OUTPUT_VAR_BASE "generate_for_pb.custom_configure" RULE_BODY
                                "${YAML_OUTPUT}")
  set(_generate_for_pb_output_var_base "generate_for_pb.custom_configure")
  set(${_generate_for_pb_output_var_base}_SOURCE_FILES "${${_generate_for_pb_output_var_base}_SOURCE_FILES}" PARENT_SCOPE)
  set(${_generate_for_pb_output_var_base}_HEADER_FILES "${${_generate_for_pb_output_var_base}_HEADER_FILES}" PARENT_SCOPE)
endfunction()

function(generate_for_pb_add_ss_service SERVICE_NAME SERVICE_ROOT_DIR)
  set(GENERATE_FOR_PB_ARGS_OPTIONS RPC_IGNORE_EMPTY_REQUEST NO_RPC NO_SERVICE_TASK)
  set(GENERATE_FOR_PB_ARGS_ONE_VALUE TASK_PATH_PREFIX HANDLE_PATH_PREFIX PROJECT_NAMESPACE RPC_ROOT_DIR
                                     SERVICE_DLLEXPORT_DECL RPC_DLLEXPORT_DECL GENERATED_OUTPUT_FILES)
  set(GENERATE_FOR_PB_ARGS_MULTI_VALUE INCLUDE_HEADERS EXTERNAL_PROTOCOLS EXTERNAL_COMPONENT_PROTOCOLS EXTERNAL_SERVICE_PROTOCOLS)

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
  if(NOT GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR)
    set(GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR "${PROJECT_SERVER_FRAME_BAS_DIR}")
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

  set(GENERATE_FOR_PB_RULE_BODY)
  set(GENERATE_FOR_PB_TEMPLATE_DEPENDS)
  if(NOT GENERATE_FOR_PB_ARGS_NO_RPC)
    string(
      APPEND GENERATE_FOR_PB_RULE_BODY
      "  # ${SERVICE_NAME} - rpc
  - service:
      name: '${SERVICE_NAME}'
      overwrite: true
      output_directory: '${GENERATE_FOR_PB_ARGS_RPC_ROOT_DIR}'
      service_dllexport_decl: '${GENERATE_FOR_PB_ARGS_SERVICE_DLLEXPORT_DECL}'
      rpc_dllexport_decl: '${GENERATE_FOR_PB_ARGS_RPC_DLLEXPORT_DECL}'
      custom_variables:
        project_namespace: '${GENERATE_FOR_PB_ARGS_PROJECT_NAMESPACE}'
        rpc_include_prefix: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}'
        ${CUSTOM_INCLUDE_HEADERS}
      service_template:
        - overwrite: true
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/rpc_call_api_for_ss.h.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/rpc_call_api_for_ss.cpp.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp'
")
    list(APPEND GENERATE_FOR_PB_TEMPLATE_DEPENDS "${GENERATE_FOR_PB_SOURCE_DIR}/templates/rpc_call_api_for_ss.h.mako"
                "${GENERATE_FOR_PB_SOURCE_DIR}/templates/rpc_call_api_for_ss.cpp.mako")
  endif()
  if(NOT GENERATE_FOR_PB_ARGS_NO_SERVICE_TASK)
    string(
      APPEND GENERATE_FOR_PB_RULE_BODY
      "  # ${SERVICE_NAME} - task
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
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_ss_rpc.h.mako'
          output: '${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_ss_rpc.cpp.mako'
          output: '${HANDLE_PATH_PREFIX}handle_ss_rpc_\${service.get_name_lower_rule()}.cpp'
      ${GENERATE_FOR_PB_RPC_IGNORE_EMPTY_REQUEST}
      rpc_template:
        - overwrite: false
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_ss_rpc.h.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h'
        - overwrite: false
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_ss_rpc.cpp.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp'
")
    list(APPEND GENERATE_FOR_PB_TEMPLATE_DEPENDS "${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_ss_rpc.h.mako"
                "${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_ss_rpc.cpp.mako"
                "${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_ss_rpc.h.mako"
                "${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_ss_rpc.cpp.mako")
  endif()

  generate_for_pb_resolve_external_pb_inputs(
    GENERATE_FOR_PB_EXTERNAL_PB_FILES
    RAW_PB_FILES
    ${GENERATE_FOR_PB_ARGS_EXTERNAL_PROTOCOLS}
    COMPONENT_PROTOCOLS
    ${GENERATE_FOR_PB_ARGS_EXTERNAL_COMPONENT_PROTOCOLS}
    SERVICE_PROTOCOLS
    ${GENERATE_FOR_PB_ARGS_EXTERNAL_SERVICE_PROTOCOLS})
  generate_for_pb_register_flow(
    "${SERVICE_NAME}"
    OUTPUT_VAR_BASE
    "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}"
    RULE_BODY
    "${GENERATE_FOR_PB_RULE_BODY}"
    TEMPLATE_DEPENDS
    ${GENERATE_FOR_PB_TEMPLATE_DEPENDS}
    EXTERNAL_PB_FILES
    ${GENERATE_FOR_PB_EXTERNAL_PB_FILES})
  if(GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES)
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}")
  else()
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${SERVICE_NAME}")
  endif()
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES}" PARENT_SCOPE)
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES}" PARENT_SCOPE)
endfunction(generate_for_pb_add_ss_service)

function(generate_for_pb_add_cs_service SERVICE_NAME SERVICE_ROOT_DIR)
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
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/session_downstream_api_for_cs.h.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/session_downstream_api_for_cs.cpp.mako'
          output: 'rpc/\${service.get_extension_field(\"service_options\", lambda x: x.module_name, service.get_name_lower_rule())}/\${service.get_name_lower_rule()}.cpp'
        - overwrite: true
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_cs_rpc.h.mako'
          output: '${HANDLE_PATH_PREFIX}handle_cs_rpc_\${service.get_name_lower_rule()}.h'
        - overwrite: true
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_cs_rpc.cpp.mako'
          output: '${HANDLE_PATH_PREFIX}handle_cs_rpc_\${service.get_name_lower_rule()}.cpp'
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
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_cs_rpc.h.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.h'
        - overwrite: false
          input: '${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_cs_rpc.cpp.mako'
          output: '${GENERATE_FOR_PB_ARGS_TASK_PATH_PREFIX}/\${rpc.get_extension_field(\"rpc_options\", lambda x: x.module_name, \"action\")}/task_action_\${rpc.get_name()}.cpp'
" )
  generate_for_pb_register_flow(
    "${SERVICE_NAME}"
    OUTPUT_VAR_BASE
    "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}"
    RULE_BODY
    "${GENERATE_FOR_PB_RULE_BODY}"
    TEMPLATE_DEPENDS
    "${GENERATE_FOR_PB_SOURCE_DIR}/templates/session_downstream_api_for_cs.h.mako"
    "${GENERATE_FOR_PB_SOURCE_DIR}/templates/session_downstream_api_for_cs.cpp.mako"
    "${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_cs_rpc.h.mako"
    "${GENERATE_FOR_PB_SOURCE_DIR}/templates/handle_cs_rpc.cpp.mako"
    "${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_cs_rpc.h.mako"
    "${GENERATE_FOR_PB_SOURCE_DIR}/templates/task_action_cs_rpc.cpp.mako")
  if(GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES)
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${GENERATE_FOR_PB_ARGS_GENERATED_OUTPUT_FILES}")
  else()
    set(GENERATE_FOR_PB_OUTPUT_VAR_BASE "${SERVICE_NAME}")
  endif()
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_SOURCE_FILES}" PARENT_SCOPE)
  set(${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES "${${GENERATE_FOR_PB_OUTPUT_VAR_BASE}_HEADER_FILES}" PARENT_SCOPE)
endfunction(generate_for_pb_add_cs_service)

function(generate_for_pb_initialize_sh SCRIPT_PATH)
  set(optionArgs COMMAND_ECHO STOP_ON_ERROR)
  set(oneValueArgs SET_LOCATION)
  set(multiValueArgs "")
  cmake_parse_arguments(generate_for_pb_initialize_sh "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  file(WRITE "${SCRIPT_PATH}" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

  if(generate_for_pb_initialize_sh_STOP_ON_ERROR)
    file(APPEND "${SCRIPT_PATH}" "set -e ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()
  if(generate_for_pb_initialize_sh_COMMAND_ECHO)
    file(APPEND "${SCRIPT_PATH}" "set -x ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()
  if(generate_for_pb_initialize_sh_SET_LOCATION)
    file(APPEND "${SCRIPT_PATH}"
         "cd \"${generate_for_pb_initialize_sh_SET_LOCATION}\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()

  project_make_executable("${SCRIPT_PATH}")
endfunction()

function(generate_for_pb_initialize_pwsh SCRIPT_PATH)
  set(optionArgs COMMAND_ECHO STOP_ON_ERROR)
  set(oneValueArgs SET_LOCATION)
  set(multiValueArgs "")
  cmake_parse_arguments(generate_for_pb_initialize_pwsh "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  file(WRITE "${SCRIPT_PATH}" "#!/usr/bin/env pwsh${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  file(APPEND "${SCRIPT_PATH}"
       "$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  file(APPEND "${SCRIPT_PATH}"
       "$OutputEncoding = [System.Text.UTF8Encoding]::new()${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")

  if(generate_for_pb_initialize_pwsh_STOP_ON_ERROR)
    file(APPEND "${SCRIPT_PATH}" "$ErrorActionPreference = \"Stop\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()
  if(generate_for_pb_initialize_pwsh_COMMAND_ECHO)
    file(APPEND "${SCRIPT_PATH}" "Set-PSDebug -Trace 1 ${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()

  if(generate_for_pb_initialize_pwsh_SET_LOCATION)
    file(APPEND "${SCRIPT_PATH}"
         "Set-Location \"${generate_for_pb_initialize_pwsh_SET_LOCATION}\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
  endif()

  project_make_executable("${SCRIPT_PATH}")
endfunction()

function(generate_for_pb_run_generator)
  get_property(_generate_for_pb_finalized GLOBAL PROPERTY "GENERATE_FOR_PB_FINALIZED")
  if(_generate_for_pb_finalized)
    return()
  endif()

  generate_for_pb_get_global_list("GENERATE_FOR_PB_FLOW_IDS" _generate_for_pb_flow_ids)
  if(NOT _generate_for_pb_flow_ids)
    set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FINALIZED" TRUE)
    return()
  endif()

  generate_for_pb_refresh_conf_files()
  generate_for_pb_collect_proto_inputs(_generate_for_pb_proto_paths _generate_for_pb_proto_files)
  if(NOT _generate_for_pb_proto_files)
    message(FATAL_ERROR "No protocol files were registered for generate-for-pb.")
  endif()
  generate_for_pb_get_builtin_proto_files(_generate_for_pb_builtin_proto_files)

  set(_generate_for_pb_proto_path_args)
  foreach(_generate_for_pb_proto_path IN LISTS _generate_for_pb_proto_paths)
    list(APPEND _generate_for_pb_proto_path_args --proto_path "${_generate_for_pb_proto_path}")
  endforeach()

  set(_generate_for_pb_proto_target "${GENERATE_FOR_PB_TARGET}-proto")
  if(NOT TARGET ${_generate_for_pb_proto_target})
    add_custom_command(
      OUTPUT "${GENERATE_FOR_PB_OUT_PB}" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_GENERATED_PBD_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_INSTALL_RES_PBD_DIR}"
      COMMAND
        "${GENERATE_FOR_PB_PROROC_BIN}" -o "${GENERATE_FOR_PB_OUT_PB}" --proto_path
        "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}" --proto_path "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
        --proto_path "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include" ${_generate_for_pb_proto_path_args}
        ${_generate_for_pb_proto_files} ${_generate_for_pb_builtin_proto_files}
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${GENERATE_FOR_PB_OUT_PB}" "${PROJECT_INSTALL_RES_PBD_DIR}"
      WORKING_DIRECTORY "${GENERATE_FOR_PB_WORK_DIR}"
      DEPENDS ${_generate_for_pb_proto_files} ${_generate_for_pb_builtin_proto_files} "${GENERATE_FOR_PB_PROROC_BIN}"
      COMMENT "Generate [@${GENERATE_FOR_PB_WORK_DIR}] ${GENERATE_FOR_PB_OUT_PB}")
    add_custom_target(${_generate_for_pb_proto_target}
                      DEPENDS "${GENERATE_FOR_PB_OUT_PB}" "${PROJECT_INSTALL_RES_PBD_DIR}/network.pb")
    set_property(TARGET ${_generate_for_pb_proto_target} PROPERTY FOLDER "${PROJECT_NAME}/codegen")
    add_dependencies(${GENERATE_FOR_PB_TARGET} ${_generate_for_pb_proto_target})
  endif()

  generate_for_pb_get_global_list("GENERATE_FOR_PB_EXTERNAL_PB_FILES" _generate_for_pb_external_pb_files)
  generate_for_pb_get_global_list("GENERATE_FOR_PB_PROTOCOL_CODEGEN_TARGETS" _generate_for_pb_protocol_codegen_targets)
  foreach(_generate_for_pb_flow_id IN LISTS _generate_for_pb_flow_ids)
    get_property(_generate_for_pb_outputs GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_OUTPUTS_${_generate_for_pb_flow_id}")
    if(NOT _generate_for_pb_outputs)
      continue()
    endif()

    get_property(_generate_for_pb_conf_file GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_CONF_FILE_${_generate_for_pb_flow_id}")
    get_property(_generate_for_pb_flow_name GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_NAME_${_generate_for_pb_flow_id}")
    get_property(_generate_for_pb_template_depends GLOBAL PROPERTY
                 "GENERATE_FOR_PB_FLOW_TEMPLATE_DEPENDS_${_generate_for_pb_flow_id}")
    get_property(_generate_for_pb_target_name GLOBAL PROPERTY "GENERATE_FOR_PB_FLOW_TARGET_${_generate_for_pb_flow_id}")
    if(NOT _generate_for_pb_target_name)
      set(_generate_for_pb_target_name "${GENERATE_FOR_PB_TARGET}-${_generate_for_pb_flow_id}")
    endif()

    if(TARGET ${_generate_for_pb_target_name})
      foreach(_generate_for_pb_protocol_codegen_target IN LISTS _generate_for_pb_protocol_codegen_targets)
        if(TARGET ${_generate_for_pb_protocol_codegen_target})
          add_dependencies(${_generate_for_pb_target_name} ${_generate_for_pb_protocol_codegen_target})
        endif()
      endforeach()
      add_dependencies(${GENERATE_FOR_PB_TARGET} ${_generate_for_pb_target_name})
      continue()
    endif()

    add_custom_command(
      OUTPUT ${_generate_for_pb_outputs}
      COMMAND
        "${Python3_EXECUTABLE}" "${GENERATE_FOR_PB_PY}" --add-package-prefix
        "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}" --clang-format-path "${PROJECT_TOOL_CLANG_FORMAT}" -c
        "${_generate_for_pb_conf_file}"
      WORKING_DIRECTORY "${GENERATE_FOR_PB_WORK_DIR}"
      DEPENDS "${GENERATE_FOR_PB_OUT_PB}" "${_generate_for_pb_conf_file}" "${GENERATE_FOR_PB_PY}"
              ${_generate_for_pb_template_depends} ${_generate_for_pb_external_pb_files}
      COMMENT "Generate [@${GENERATE_FOR_PB_WORK_DIR}] ${_generate_for_pb_flow_name}")
    add_custom_target(${_generate_for_pb_target_name} DEPENDS ${_generate_for_pb_outputs})
    add_dependencies(${_generate_for_pb_target_name} ${_generate_for_pb_proto_target})
    foreach(_generate_for_pb_protocol_codegen_target IN LISTS _generate_for_pb_protocol_codegen_targets)
      if(TARGET ${_generate_for_pb_protocol_codegen_target})
        add_dependencies(${_generate_for_pb_target_name} ${_generate_for_pb_protocol_codegen_target})
      endif()
    endforeach()
    set_property(TARGET ${_generate_for_pb_target_name} PROPERTY FOLDER "${PROJECT_NAME}/codegen")
    add_dependencies(${GENERATE_FOR_PB_TARGET} ${_generate_for_pb_target_name})
  endforeach()

  set(GENERATE_FOR_PB_PROTO_COMMAND "${GENERATE_FOR_PB_TARGET}" PARENT_SCOPE)
  set_property(GLOBAL PROPERTY "GENERATE_FOR_PB_FINALIZED" TRUE)
endfunction()
