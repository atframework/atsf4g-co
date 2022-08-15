function(project_service_declare_sdk TARGET_NAME SDK_ROOT_DIR)
  set(optionArgs "")
  set(oneValueArgs INCLUDE_DIR OUTPUT_NAME OUTPUT_TARGET_NAME)
  set(multiValueArgs HRADERS SOURCES USE_COMPONENTS USE_SERVICE_SDK USE_SERVICE_PROTOCOL)
  cmake_parse_arguments(project_service_declare_sdk "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(TARGET_FULL_NAME "ps-${TARGET_NAME}")
  else()
    set(TARGET_FULL_NAME "${PROJECT_NAME}-service-${TARGET_NAME}")
  endif()
  echowithcolor(COLOR GREEN "-- Configure sdk::${TARGET_NAME} on ${SDK_ROOT_DIR}")

  if(project_service_declare_sdk_SOURCES)
    source_group_by_dir(project_service_declare_sdk_HRADERS project_service_declare_sdk_SOURCES)
    if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows|MinGW|WindowsStore" AND (BUILD_SHARED_LIBS
                                                                       OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY))
      add_library(${TARGET_FULL_NAME} SHARED ${project_service_declare_sdk_HRADERS}
                                             ${project_service_declare_sdk_SOURCES})
    else()
      add_library(${TARGET_FULL_NAME} STATIC ${project_service_declare_sdk_HRADERS}
                                             ${project_service_declare_sdk_SOURCES})
    endif()
    set_target_properties(${TARGET_FULL_NAME} PROPERTIES BUILD_RPATH_USE_ORIGIN YES)
    target_compile_options(${TARGET_FULL_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
    if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
      target_link_options(${TARGET_FULL_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
    endif()
  else()
    add_library(${TARGET_FULL_NAME} INTERFACE)
  endif()

  if(project_service_declare_sdk_OUTPUT_NAME)
    set_target_properties(${TARGET_FULL_NAME} PROPERTIES OUTPUT_NAME "${project_service_declare_sdk_OUTPUT_NAME}")
  endif()
  if(project_service_declare_sdk_OUTPUT_TARGET_NAME)
    set(${project_service_declare_sdk_OUTPUT_TARGET_NAME}
        "${TARGET_FULL_NAME}"
        PARENT_SCOPE)
  endif()

  if(project_service_declare_sdk_HRADERS AND project_service_declare_sdk_INCLUDE_DIR)
    if(project_service_declare_sdk_SOURCES)
      target_include_directories(${TARGET_FULL_NAME}
                                 PUBLIC "$<BUILD_INTERFACE:${project_service_declare_sdk_INCLUDE_DIR}>")
    else()
      target_include_directories(${TARGET_FULL_NAME}
                                 INTERFACE "$<BUILD_INTERFACE:${project_service_declare_sdk_INCLUDE_DIR}>")
    endif()
  endif()

  unset(PUBLIC_LINK_TARGETS)
  unset(INTERFACE_LINK_TARGETS)
  if(project_service_declare_sdk_USE_COMPONENTS)
    foreach(USE_COMPONENT ${project_service_declare_sdk_USE_COMPONENTS})
      list(APPEND PUBLIC_LINK_TARGETS "components::${USE_COMPONENT}")
    endforeach()
  endif()
  if(project_service_declare_sdk_USE_SERVICE_PROTOCOL)
    foreach(USE_SERVICE_PROTOCOL ${project_service_declare_sdk_USE_SERVICE_PROTOCOL})
      list(APPEND PUBLIC_LINK_TARGETS "protocol::${USE_SERVICE_PROTOCOL}")
    endforeach()
  endif()
  if(project_service_declare_sdk_USE_SERVICE_SDK)
    foreach(USE_SERVICE_SDK ${project_service_declare_sdk_USE_SERVICE_SDK})
      list(APPEND PUBLIC_LINK_TARGETS "sdk::${USE_SERVICE_SDK}")
    endforeach()
  endif()
  list(APPEND PUBLIC_LINK_TARGETS ${PROJECT_SERVER_FRAME_LIB_LINK})
  if(project_service_declare_sdk_SOURCES)
    if(INTERFACE_LINK_TARGETS)
      target_link_libraries(${TARGET_FULL_NAME} INTERFACE ${PUBLIC_LINK_TARGETS})
    endif()
    target_link_libraries(${TARGET_FULL_NAME} PUBLIC ${PUBLIC_LINK_TARGETS})
  elseif(project_service_declare_sdk_HRADERS)
    target_link_libraries(${TARGET_FULL_NAME} INTERFACE ${INTERFACE_LINK_TARGETS} ${PUBLIC_LINK_TARGETS})
  endif()

  install(
    TARGETS ${TARGET_FULL_NAME}
    EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}")

  if(project_service_declare_sdk_HRADERS AND project_service_declare_sdk_INCLUDE_DIR)
    install(
      DIRECTORY ${project_service_declare_sdk_INCLUDE_DIR}
      TYPE INCLUDE
      USE_SOURCE_PERMISSIONS FILES_MATCHING
      REGEX ".+\\.h(pp)?$"
      PATTERN ".svn" EXCLUDE
      PATTERN ".git" EXCLUDE)
  endif()

  add_library("sdk::${TARGET_NAME}" ALIAS "${TARGET_FULL_NAME}")

  if(MSVC)
    set_property(TARGET "${TARGET_FULL_NAME}" PROPERTY FOLDER "${PROJECT_NAME}/service/sdk")
  endif()
endfunction()

function(project_service_force_optimize_sources)
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

function(project_service_declare_protocol TARGET_NAME PROTOCOL_DIR)
  set(optionArgs "")
  set(oneValueArgs OUTPUT_DIR OUTPUT_NAME OUTPUT_TARGET_NAME)
  set(multiValueArgs PROTOCOLS USE_COMPONENTS USE_SERVICE_PROTOCOL)
  cmake_parse_arguments(project_service_declare_protocol "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(project_service_declare_protocol_OUTPUT_DIR)
    file(MAKE_DIRECTORY "${project_service_declare_protocol_OUTPUT_DIR}")
  else()
    set(project_service_declare_protocol_OUTPUT_DIR "${PROTOCOL_DIR}")
  endif()

  if(NOT project_service_declare_protocol_PROTOCOLS)
    message(FATAL_ERROR "PROTOCOLS is required for project_service_declare_protocol")
  endif()
  echowithcolor(COLOR GREEN "-- Configure protocol::${TARGET_NAME} on ${PROTOCOL_DIR}")

  unset(FINAL_GENERATED_SOURCE_FILES)
  unset(FINAL_GENERATED_HEADER_FILES)
  set(FINAL_GENERATED_LAST_CREATED_DIR ".")
  unset(FINAL_GENERATED_COPY_COMMANDS)
  list(SORT project_service_declare_protocol_PROTOCOLS)
  foreach(PROTO_FILE ${project_service_declare_protocol_PROTOCOLS})
    file(RELATIVE_PATH RELATIVE_FILE_PATH "${PROTOCOL_DIR}" "${PROTO_FILE}")
    string(REGEX REPLACE "\\.proto$" "" RELATIVE_FILE_PREFIX "${RELATIVE_FILE_PATH}")
    list(APPEND FINAL_GENERATED_HEADER_FILES
         "${project_service_declare_protocol_OUTPUT_DIR}/${RELATIVE_FILE_PREFIX}.pb.h")
    list(APPEND FINAL_GENERATED_SOURCE_FILES
         "${project_service_declare_protocol_OUTPUT_DIR}/${RELATIVE_FILE_PREFIX}.pb.cc")
    get_filename_component(FINAL_GENERATED_SOURCE_DIR
                           "${project_service_declare_protocol_OUTPUT_DIR}/${RELATIVE_FILE_PREFIX}.pb.cc" DIRECTORY)
    if(NOT FINAL_GENERATED_LAST_CREATED_DIR STREQUAL FINAL_GENERATED_SOURCE_DIR)
      if(NOT EXISTS "${FINAL_GENERATED_SOURCE_DIR}")
        file(MAKE_DIRECTORY "${FINAL_GENERATED_SOURCE_DIR}")
      endif()
      set(FINAL_GENERATED_LAST_CREATED_DIR "${FINAL_GENERATED_SOURCE_DIR}")

      if(FINAL_GENERATED_COPY_COMMANDS)
        list(APPEND FINAL_GENERATED_COPY_COMMANDS "${FINAL_GENERATED_LAST_CREATED_DIR}")
      endif()
      list(
        APPEND
        FINAL_GENERATED_COPY_COMMANDS
        "COMMAND"
        "${CMAKE_COMMAND}"
        "-E"
        "copy_if_different"
        "${CMAKE_CURRENT_BINARY_DIR}/${RELATIVE_FILE_PREFIX}.pb.h"
        "${CMAKE_CURRENT_BINARY_DIR}/${RELATIVE_FILE_PREFIX}.pb.cc")
    else()
      list(APPEND FINAL_GENERATED_COPY_COMMANDS "${CMAKE_CURRENT_BINARY_DIR}/${RELATIVE_FILE_PREFIX}.pb.h"
           "${CMAKE_CURRENT_BINARY_DIR}/${RELATIVE_FILE_PREFIX}.pb.cc")
    endif()
  endforeach()
  if(FINAL_GENERATED_COPY_COMMANDS)
    list(APPEND FINAL_GENERATED_COPY_COMMANDS "${FINAL_GENERATED_LAST_CREATED_DIR}")
  endif()

  set(PROTOBUF_PROTO_PATHS
      --proto_path
      "${PROTOCOL_DIR}"
      --proto_path
      "${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/proto"
      --proto_path
      --proto_path
      "${PROJECT_THIRD_PARTY_INSTALL_DIR}/include"
      --proto_path
      "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
      --proto_path
      "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include")
  if(PROJECT_COMPONENT_PUBLIC_PROTO_PATH)
    foreach(PROTO_PATH ${PROJECT_COMPONENT_PUBLIC_PROTO_PATH})
      list(APPEND PROTOBUF_PROTO_PATHS "--proto_path" "${PROTO_PATH}")
    endforeach()
  endif()
  if(PROJECT_SERVICE_PUBLIC_PROTO_PATH)
    foreach(PROTO_PATH ${PROJECT_COMPONENT_PUBLIC_PROTO_PATH})
      list(APPEND PROTOBUF_PROTO_PATHS "--proto_path" "${PROTO_PATH}")
    endforeach()
  endif()
  unset(PUBLIC_LINK_TARGETS)
  unset(INTERFACE_LINK_TARGETS)
  if(project_service_declare_protocol_USE_COMPONENTS)
    foreach(USE_COMPONENT ${project_service_declare_protocol_USE_COMPONENTS})
      get_target_property(FIND_PROTO_DIR "components::${USE_COMPONENT}" PORJECT_PROTOCOL_DIR)
      if(FIND_PROTO_DIR)
        list(APPEND PROTOBUF_PROTO_PATHS --proto_path "${FIND_PROTO_DIR}")
      endif()
      list(APPEND PUBLIC_LINK_TARGETS "components::${USE_COMPONENT}")
    endforeach()
  endif()
  if(project_service_declare_protocol_USE_SERVICE_PROTOCOL)
    foreach(USE_SERVICE_PROTOCOL ${project_service_declare_protocol_USE_SERVICE_PROTOCOL})
      get_target_property(FIND_PROTO_DIR "protocol::${USE_SERVICE_PROTOCOL}" PORJECT_PROTOCOL_DIR)
      if(FIND_PROTO_DIR)
        list(APPEND PROTOBUF_PROTO_PATHS --proto_path "${FIND_PROTO_DIR}")
      endif()
      list(APPEND PUBLIC_LINK_TARGETS "protocol::${USE_SERVICE_PROTOCOL}")
    endforeach()
  endif()

  add_custom_command(
    OUTPUT ${FINAL_GENERATED_SOURCE_FILES} ${FINAL_GENERATED_HEADER_FILES}
    COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" ${PROTOBUF_PROTO_PATHS} --cpp_out
      "${CMAKE_CURRENT_BINARY_DIR}"
      # Protocol buffer files
      ${project_service_declare_protocol_PROTOCOLS} ${FINAL_GENERATED_COPY_COMMANDS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${project_service_declare_protocol_PROTOCOLS} "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}"
    COMMENT "Generate [@${CMAKE_CURRENT_BINARY_DIR}] ${FINAL_GENERATED_SOURCE_FILES};${FINAL_GENERATED_HEADER_FILES}")

  project_build_tools_patch_protobuf_sources(${FINAL_GENERATED_SOURCE_FILES} ${FINAL_GENERATED_HEADER_FILES})
  project_service_force_optimize_sources(${FINAL_GENERATED_SOURCE_FILES} ${FINAL_GENERATED_HEADER_FILES})

  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(TARGET_FULL_NAME "pp-${TARGET_NAME}")
  else()
    set(TARGET_FULL_NAME "${PROJECT_NAME}-protocol-${TARGET_NAME}")
  endif()
  source_group_by_dir(FINAL_GENERATED_SOURCE_FILES FINAL_GENERATED_HEADER_FILES)
  if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows|MinGW|WindowsStore" AND (BUILD_SHARED_LIBS
                                                                     OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY))
    add_library(${TARGET_FULL_NAME} SHARED ${FINAL_GENERATED_SOURCE_FILES} ${FINAL_GENERATED_HEADER_FILES})
  else()
    add_library(${TARGET_FULL_NAME} STATIC ${FINAL_GENERATED_SOURCE_FILES} ${FINAL_GENERATED_HEADER_FILES})
  endif()
  set_target_properties(
    ${TARGET_FULL_NAME}
    PROPERTIES C_VISIBILITY_PRESET "default"
               CXX_VISIBILITY_PRESET "default"
               VERSION "${PROJECT_VERSION}"
               WINDOWS_EXPORT_ALL_SYMBOLS TRUE
               BUILD_RPATH_USE_ORIGIN YES
               PORJECT_PROTOCOL_DIR "${PROTOCOL_DIR}")

  target_compile_options(${TARGET_FULL_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
    target_link_options(${TARGET_FULL_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
  endif()

  if(project_service_declare_protocol_OUTPUT_NAME)
    set_target_properties(${TARGET_FULL_NAME} PROPERTIES OUTPUT_NAME "${project_service_declare_protocol_OUTPUT_NAME}")
  endif()
  if(project_service_declare_protocol_OUTPUT_TARGET_NAME)
    set(${project_service_declare_protocol_OUTPUT_TARGET_NAME}
        "${TARGET_FULL_NAME}"
        PARENT_SCOPE)
  endif()

  target_include_directories(
    ${TARGET_FULL_NAME}
    PUBLIC "$<BUILD_INTERFACE:${project_service_declare_protocol_OUTPUT_DIR}>"
           "$<BUILD_INTERFACE:${PROJECT_SERVER_FRAME_PROTOCOL_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

  list(APPEND PUBLIC_LINK_TARGETS ${PROJECT_SERVER_FRAME_LIB_LINK}-protocol
       ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME})
  if(INTERFACE_LINK_TARGETS)
    target_link_libraries(${TARGET_FULL_NAME} INTERFACE ${PUBLIC_LINK_TARGETS})
  endif()
  target_link_libraries(${TARGET_FULL_NAME} PUBLIC ${PUBLIC_LINK_TARGETS})

  install(
    TARGETS ${TARGET_FULL_NAME}
    EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}")

  install(
    DIRECTORY ${project_service_declare_protocol_OUTPUT_DIR}
    TYPE INCLUDE
    USE_SOURCE_PERMISSIONS FILES_MATCHING
    REGEX ".+\\.pb\\.h?$"
    PATTERN ".svn" EXCLUDE
    PATTERN ".git" EXCLUDE)

  add_library("protocol::${TARGET_NAME}" ALIAS "${TARGET_FULL_NAME}")
  if(MSVC)
    set_property(TARGET "${TARGET_FULL_NAME}" PROPERTY FOLDER "${PROJECT_NAME}/service/protocol")
  endif()
endfunction()

function(project_service_declare_instance TARGET_NAME SERVICE_ROOT_DIR)
  set(optionArgs "")
  set(oneValueArgs INCLUDE_DIR OUTPUT_NAME OUTPUT_TARGET_NAME RUNTIME_OUTPUT_DIRECTORY)
  set(multiValueArgs
      HRADERS
      SOURCES
      RESOURCE_DIRECTORIES
      RESOURCE_FILES
      USE_COMPONENTS
      USE_SERVICE_SDK
      USE_SERVICE_PROTOCOL)
  cmake_parse_arguments(project_service_declare_instance "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  echowithcolor(COLOR GREEN "-- Configure service ${TARGET_NAME} on ${SERVICE_ROOT_DIR}")

  source_group_by_dir(project_service_declare_instance_HRADERS project_service_declare_instance_SOURCES)
  add_executable(${TARGET_NAME} ${project_service_declare_instance_HRADERS} ${project_service_declare_instance_SOURCES})

  target_compile_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
    target_link_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
  endif()
  if(project_service_declare_instance_OUTPUT_NAME)
    set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${project_service_declare_instance_OUTPUT_NAME}"
                                                    BUILD_RPATH_USE_ORIGIN YES)
  else()
    set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${TARGET_NAME}d" BUILD_RPATH_USE_ORIGIN YES)
  endif()
  if(NOT project_service_declare_instance_RUNTIME_OUTPUT_DIRECTORY)
    set(project_service_declare_instance_RUNTIME_OUTPUT_DIRECTORY "${TARGET_NAME}/bin")
  endif()

  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY
               "${PROJECT_INSTALL_BAS_DIR}/${project_service_declare_instance_RUNTIME_OUTPUT_DIRECTORY}"
               PDB_OUTPUT_DIRECTORY
               "${PROJECT_INSTALL_BAS_DIR}/${project_service_declare_instance_RUNTIME_OUTPUT_DIRECTORY}")

  if(project_service_declare_instance_OUTPUT_TARGET_NAME)
    set(${project_service_declare_instance_OUTPUT_TARGET_NAME}
        "${TARGET_NAME}"
        PARENT_SCOPE)
  endif()

  target_include_directories(${TARGET_NAME} PRIVATE "$<BUILD_INTERFACE:${SERVICE_ROOT_DIR}>")
  if(project_service_declare_instance_INCLUDE_DIR)
    target_include_directories(${TARGET_NAME}
                               PRIVATE "$<BUILD_INTERFACE:${project_service_declare_instance_INCLUDE_DIR}>")
  endif()

  unset(LINK_TARGETS)
  if(project_service_declare_instance_USE_COMPONENTS)
    foreach(USE_COMPONENT ${project_service_declare_instance_USE_COMPONENTS})
      list(APPEND LINK_TARGETS "components::${USE_COMPONENT}")
    endforeach()
  endif()
  if(project_service_declare_instance_USE_SERVICE_PROTOCOL)
    foreach(USE_SERVICE_PROTOCOL ${project_service_declare_instance_USE_SERVICE_PROTOCOL})
      list(APPEND LINK_TARGETS "protocol::${USE_SERVICE_PROTOCOL}")
    endforeach()
  endif()
  if(project_service_declare_instance_USE_SERVICE_SDK)
    foreach(USE_SERVICE_SDK ${project_service_declare_instance_USE_SERVICE_SDK})
      list(APPEND LINK_TARGETS "sdk::${USE_SERVICE_SDK}")
    endforeach()
  endif()
  list(APPEND LINK_TARGETS ${PROJECT_SERVER_FRAME_LIB_LINK})
  target_link_libraries(${TARGET_NAME} PRIVATE ${LINK_TARGETS})

  if(MSVC)
    set_property(TARGET "${TARGET_NAME}" PROPERTY FOLDER "${PROJECT_NAME}/service/server")
  endif()

  install(
    TARGETS ${TARGET_NAME}
    EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
    RUNTIME DESTINATION "${project_service_declare_instance_RUNTIME_OUTPUT_DIRECTORY}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}")

  if(project_service_declare_instance_RESOURCE_DIRECTORIES)
    foreach(RESOURCE_DIRECTORY ${project_service_declare_instance_RESOURCE_DIRECTORIES})
      if(NOT EXISTS "${PROJECT_INSTALL_BAS_DIR}/${RESOURCE_DIRECTORY}")
        file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${RESOURCE_DIRECTORY}")
      endif()
      install(
        DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${RESOURCE_DIRECTORY}"
        DESTINATION "${RESOURCE_DIRECTORY}"
        USE_SOURCE_PERMISSIONS FILES_MATCHING
        PATTERN ".svn" EXCLUDE
        PATTERN ".git" EXCLUDE)
    endforeach()
  endif()
  if(project_service_declare_instance_RESOURCE_FILES)
    foreach(RESOURCE_FILE ${project_service_declare_instance_RESOURCE_FILES})
      get_filename_component(RESOURCE_FILE_DIR "${RESOURCE_FILE}" DIRECTORY)
      install(FILES "${PROJECT_INSTALL_BAS_DIR}/${RESOURCE_FILE}" DESTINATION "${RESOURCE_FILE_DIR}")
    endforeach()
  endif()
endfunction()
