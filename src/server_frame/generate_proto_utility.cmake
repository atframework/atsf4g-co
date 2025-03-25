function(project_build_tools_optimize_sources)
  if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU|Clang|AppleClang" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach(PROTO_SRC ${ARGN})
      unset(PROTO_SRC_OPTIONS)
      get_source_file_property(PROTO_SRC_OPTIONS ${PROTO_SRC} COMPILE_OPTIONS)

      if(PROTO_SRC_OPTIONS)
        list(APPEND PROTO_SRC_OPTIONS "$<$<CONFIG:Debug>:-O2>")
      else()
        set(PROTO_SRC_OPTIONS "$<$<CONFIG:Debug>:-O2>")
      endif()

      set_source_files_properties(${PROTO_SRC} PROPERTIES COMPILE_OPTIONS "${PROTO_SRC_OPTIONS}")
    endforeach()

    unset(PROTO_SRC)
    unset(PROTO_SRC_OPTIONS)
  endif()
endfunction()

function(project_build_tools_target_precompile_headers TARGET_NAME)
  if(PROJECT_ENABLE_PRECOMPILE_HEADERS AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(${TARGET_NAME} ${ARGN})
  endif()
endfunction()

function(project_server_frame_create_protocol_target TARGET_NAME SANDBOX_PATH OUTPUT_LIBRARY_TARGET_NAME)
  set(optionArgs "")
  set(oneValueArgs "")
  set(multiValueArgs EXTERNAL_PROTO_PATH DEPENDS PROTOCOLS PRIVATE_LINK_LIBRARIES PUBLIC_LINK_LIBRARIES)
  cmake_parse_arguments(project_server_frame_create_protocol_target "${optionArgs}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN})
  list(SORT project_server_frame_create_protocol_target_PROTOCOLS)
  unset(HEADERS)
  unset(SOURCES)
  unset(PCH_HEADER_FILES)

  # proto -> headers/sources
  unset(LAST_CREATED_DIRECTORY)
  unset(LAST_DIRECTORY_HEADERS)
  unset(LAST_DIRECTORY_SOURCES)
  unset(ADDITIONAL_CMAKE_COMMANDS)
  unset(TEMPORARY_CODE_FILES)
  set(FIND_PROTO_PATH_HINT "${SANDBOX_PATH}" ${project_server_frame_create_protocol_target_EXTERNAL_PROTO_PATH})

  foreach(FILE_PATH IN LISTS project_server_frame_create_protocol_target_PROTOCOLS)
    unset(FILE_RELATIVE_PATH)
    string(REPLACE "\\" "/" FILE_PATH_S "${FILE_PATH}")

    foreach(TRY_PROTO_PATH ${FIND_PROTO_PATH_HINT})
      string(REPLACE "\\" "/" TRY_PROTO_PATH_S "${TRY_PROTO_PATH}")
      string(LENGTH "${TRY_PROTO_PATH_S}/" TRY_PROTO_PATH_LENGTH)
      string(SUBSTRING "${FILE_PATH_S}" 0 ${TRY_PROTO_PATH_LENGTH} FILE_PATH_PREFIX)

      if(FILE_PATH_PREFIX STREQUAL "${TRY_PROTO_PATH_S}/")
        file(RELATIVE_PATH FILE_RELATIVE_PATH "${TRY_PROTO_PATH_S}" "${FILE_PATH_S}")
        break()
      endif()
    endforeach()

    if(NOT FILE_RELATIVE_PATH)
      message(FATAL_ERROR "${FILE_PATH} in not in one of ${FIND_PROTO_PATH_HINT}")
    endif()

    string(REGEX REPLACE "\\.proto$" "" FILE_RELATIVE_BASE "${FILE_RELATIVE_PATH}")
    get_filename_component(FILE_RELATIVE_DIR "${FILE_RELATIVE_PATH}" DIRECTORY)
    set(CURRENT_GENERATED_DIR "${PROJECT_SERVER_FRAME_PROTOCOL_SOURCE_DIR}/${TARGET_NAME}")
    string(REGEX REPLACE "[-:]" "_" CURRENT_API_MACRO "SERVER_FRAME_${TARGET_NAME}_API")
    string(TOUPPER "${CURRENT_API_MACRO}" CURRENT_API_MACRO)

    if(NOT "${FILE_RELATIVE_DIR}" STREQUAL "${LAST_CREATED_DIRECTORY}")
      if(LAST_DIRECTORY_HEADERS)
        list(
          APPEND
          ADDITIONAL_CMAKE_COMMANDS
          COMMAND
          "${CMAKE_COMMAND}"
          -E
          copy_if_different
          ${LAST_DIRECTORY_HEADERS}
          "${CURRENT_GENERATED_DIR}/include/${LAST_CREATED_DIRECTORY}")
      endif()

      if(LAST_DIRECTORY_SOURCES)
        list(
          APPEND
          ADDITIONAL_CMAKE_COMMANDS
          COMMAND
          "${CMAKE_COMMAND}"
          -E
          copy_if_different
          ${LAST_DIRECTORY_SOURCES}
          "${CURRENT_GENERATED_DIR}/src/${LAST_CREATED_DIRECTORY}")
      endif()

      set(LAST_CREATED_DIRECTORY "${FILE_RELATIVE_DIR}")

      if(NOT EXISTS "${CURRENT_GENERATED_DIR}/include/${LAST_CREATED_DIRECTORY}")
        file(MAKE_DIRECTORY "${CURRENT_GENERATED_DIR}/include/${LAST_CREATED_DIRECTORY}")
      endif()

      if(NOT EXISTS "${CURRENT_GENERATED_DIR}/src/${LAST_CREATED_DIRECTORY}")
        file(MAKE_DIRECTORY "${CURRENT_GENERATED_DIR}/src/${LAST_CREATED_DIRECTORY}")
      endif()
    endif()

    list(APPEND LAST_DIRECTORY_HEADERS "${SANDBOX_PATH}/${FILE_RELATIVE_BASE}.pb.h")
    list(APPEND LAST_DIRECTORY_SOURCES "${SANDBOX_PATH}/${FILE_RELATIVE_BASE}.pb.cc")
    list(APPEND HEADERS "${CURRENT_GENERATED_DIR}/include/${FILE_RELATIVE_BASE}.pb.h")
    list(APPEND PCH_HEADER_FILES "\"${FILE_RELATIVE_BASE}.pb.h\"")
    list(APPEND SOURCES "${CURRENT_GENERATED_DIR}/src/${FILE_RELATIVE_BASE}.pb.cc")
  endforeach()

  if(LAST_DIRECTORY_HEADERS)
    list(
      APPEND
      ADDITIONAL_CMAKE_COMMANDS
      COMMAND
      "${CMAKE_COMMAND}"
      -E
      copy_if_different
      ${LAST_DIRECTORY_HEADERS}
      "${CURRENT_GENERATED_DIR}/include/${LAST_CREATED_DIRECTORY}")
  endif()

  if(LAST_DIRECTORY_SOURCES)
    list(
      APPEND
      ADDITIONAL_CMAKE_COMMANDS
      COMMAND
      "${CMAKE_COMMAND}"
      -E
      copy_if_different
      ${LAST_DIRECTORY_SOURCES}
      "${CURRENT_GENERATED_DIR}/src/${LAST_CREATED_DIRECTORY}")
  endif()

  list(APPEND TEMPORARY_CODE_FILES ${LAST_DIRECTORY_HEADERS} ${LAST_DIRECTORY_SOURCES})

  set(PROTOC_PROTO_PATH_ARGS --proto_path "${SANDBOX_PATH}")

  foreach(ADDTIONAL_PROTO_PATH ${project_server_frame_create_protocol_target_EXTERNAL_PROTO_PATH})
    list(APPEND PROTOC_PROTO_PATH_ARGS --proto_path "${ADDTIONAL_PROTO_PATH}")
  endforeach()

  list(
    APPEND
    PROTOC_PROTO_PATH_ARGS
    --proto_path
    "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}"
    --proto_path
    "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
    --proto_path
    "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include")

  add_custom_command(
    OUTPUT ${HEADERS} ${SOURCES}
    COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" ${PROTOC_PROTO_PATH_ARGS} --cpp_out
      "dllexport_decl=${CURRENT_API_MACRO}:${SANDBOX_PATH}"
      # Protocol buffer files
      ${project_server_frame_create_protocol_target_PROTOCOLS} ${ADDITIONAL_CMAKE_COMMANDS}
    COMMAND "${CMAKE_COMMAND}" -E remove -f ${TEMPORARY_CODE_FILES}
    WORKING_DIRECTORY "${SANDBOX_PATH}"
    DEPENDS ${project_server_frame_create_protocol_target_PROTOCOLS}
            ${project_server_frame_create_protocol_target_DEPENDS}
    COMMENT "Generate [@${SANDBOX_PATH}] ${project_server_frame_create_protocol_target_PROTOCOLS}")

  add_custom_target(
    ${TARGET_NAME}
    DEPENDS ${HEADERS} ${SOURCES} ${project_server_frame_create_protocol_target_PROTOCOLS}
            ${project_server_frame_create_protocol_target_DEPENDS}
    SOURCES ${HEADERS} ${SOURCES})

  project_build_tools_patch_protobuf_sources(${HEADERS} ${SOURCES})
  # project_build_tools_optimize_sources(${HEADERS} ${SOURCES})

  source_group(TREE "${CURRENT_GENERATED_DIR}" FILES ${HEADERS} ${SOURCES})

  if(BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
    add_library(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME} SHARED ${HEADERS} ${SOURCES})

    project_tool_split_target_debug_sybmol("${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}")
    project_build_tools_set_shared_library_declaration(${CURRENT_API_MACRO}
                                                       "${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}")
  else()
    add_library(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME} STATIC ${HEADERS} ${SOURCES})
    project_build_tools_set_static_library_declaration(${CURRENT_API_MACRO}
                                                       "${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}")
  endif()
  project_build_tools_target_precompile_headers(
    ${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}
    PUBLIC
    "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${PCH_HEADER_FILES}>>"
    PRIVATE
    "<limits>"
    "<string>"
    "<type_traits>"
    "<utility>")

  add_dependencies(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME} ${TARGET_NAME})

  if(project_server_frame_create_protocol_target_PRIVATE_LINK_LIBRARIES)
    target_link_libraries(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}
                          PRIVATE ${project_server_frame_create_protocol_target_PRIVATE_LINK_LIBRARIES})
  endif()

  if(project_server_frame_create_protocol_target_PUBLIC_LINK_LIBRARIES)
    target_link_libraries(
      ${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}
      PUBLIC ${project_server_frame_create_protocol_target_PUBLIC_LINK_LIBRARIES}
             ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME})
  else()
    target_link_libraries(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}
                          PUBLIC ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME})
  endif()
  add_dependencies(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME} ${TARGET_NAME})

  set(PROJECT_SERVER_FRAME_LIB_INSTALL_RPATH
      "${PROJECT_RPATH_ORIGIN}"
      "${PROJECT_RPATH_ORIGIN}/../../${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${PROJECT_SERVER_FRAME_LIB_LINK}/${CMAKE_INSTALL_LIBDIR}"
      ${PROJECT_INSTALL_RPATH}
      "${PROJECT_RPATH_ORIGIN}/../../../${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/__shared/${CMAKE_INSTALL_LIBDIR}"
      ${PROJECT_EXTERNAL_RPATH})

  set_target_properties(
    ${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}
    PROPERTIES C_VISIBILITY_PRESET "hidden"
               CXX_VISIBILITY_PRESET "hidden"
               VERSION "${PROJECT_VERSION}"
               SOVERSION "${PROJECT_VERSION}"
               INSTALL_RPATH "${PROJECT_SERVER_FRAME_LIB_INSTALL_RPATH}")
  target_include_directories(
    ${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}
    PUBLIC "$<BUILD_INTERFACE:${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include>"
           "$<BUILD_INTERFACE:${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include>"
           "$<BUILD_INTERFACE:${CURRENT_GENERATED_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
  target_compile_options(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}
                         PRIVATE ${PROJECT_COMMON_PROTOCOL_SOURCE_COMPILE_OPTIONS})

  if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
    target_link_options(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
  endif()

  set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "${PROJECT_NAME}")
  set_property(TARGET ${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME} PROPERTY FOLDER "${PROJECT_NAME}")

  project_install_and_export_targets(${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME})

  install(
    DIRECTORY "${CURRENT_GENERATED_DIR}/include/protocol"
    DESTINATION "include"
    USE_SOURCE_PERMISSIONS FILES_MATCHING
    REGEX ".+\\.pb\\.h?$"
    PATTERN ".svn" EXCLUDE
    PATTERN ".git" EXCLUDE)

  set(${OUTPUT_LIBRARY_TARGET_NAME}
      "${PROJECT_SERVER_FRAME_LIB_LINK}-${TARGET_NAME}"
      PARENT_SCOPE)
endfunction()

function(project_server_frame_create_protocol_sandbox OUTPUT_DIR OUTPUT_VAR)
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")
  unset(OUTPUT_FILES)

  foreach(PROTO_FILE ${ARGN})
    get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME)
    file(
      CREATE_LINK "${PROTO_FILE}" "${OUTPUT_DIR}/${PROTO_NAME}"
      RESULT LINK_RESULT
      SYMBOLIC)
    if(NOT LINK_RESULT EQUAL 0 OR NOT IS_SYMLINK "${OUTPUT_DIR}/${PROTO_NAME}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROTO_FILE}" "${OUTPUT_DIR}/${PROTO_NAME}")
    endif()
    list(APPEND OUTPUT_FILES "${OUTPUT_DIR}/${PROTO_NAME}")
  endforeach()

  set(${OUTPUT_VAR}
      ${${OUTPUT_VAR}} ${OUTPUT_FILES}
      PARENT_SCOPE)
endfunction()
