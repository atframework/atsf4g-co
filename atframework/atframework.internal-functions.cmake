include_guard(GLOBAL)

# Package
file(MAKE_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}")

function(atframework_write_package_config COMPONENT_NAME)
  set(optionArgs)
  set(oneValueArgs VERSION VERSION_COMPATIBILITY INSTALL_DESTINATION)
  set(multiValueArgs)
  cmake_parse_arguments(__atfw_pkg_config "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT __atfw_pkg_config_VERSION_COMPATIBILITY)
    set(__atfw_pkg_config_VERSION_COMPATIBILITY SameMajorVersion)
  endif()
  if(NOT __atfw_pkg_config_VERSION)
    set(__atfw_pkg_config_VERSION ${PROJECT_VERSION})
  endif()
  if(NOT __atfw_pkg_config_INSTALL_DESTINATION)
    set(__atfw_pkg_config_INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}")
  endif()

  include(GNUInstallDirs)
  set(ATFRAMEWORK_EXPORT_COMPONENT_NAME "${COMPONENT_NAME}")
  if(SERVER_FRAME_VCS_COMMIT_SHORT_SHA)
    set(ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR
        "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}")
  else()
    set(ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
  endif()
  set(ATFRAMEWORK_EXPORT_CMAKE_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
  configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/atframework-config.cmake.in"
    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}/${COMPONENT_NAME}-config.cmake"
    INSTALL_DESTINATION "${__atfw_pkg_config_INSTALL_DESTINATION}"
    PATH_VARS ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR ATFRAMEWORK_EXPORT_CMAKE_INSTALL_INCLUDEDIR)

  write_basic_package_version_file(
    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}/${COMPONENT_NAME}-config-version.cmake"
    VERSION ${__atfw_pkg_config_VERSION}
    COMPATIBILITY ${__atfw_pkg_config_VERSION_COMPATIBILITY})

  if(COMPONENT_NAME STREQUAL ATFRAMEWORK_EXPORT_PACKAGE_NAME)
    install(
      FILES
        "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}/${COMPONENT_NAME}-config.cmake"
        "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}/${COMPONENT_NAME}-config-version.cmake"
      DESTINATION "${__atfw_pkg_config_INSTALL_DESTINATION}")
  else()
    install(
      FILES
        "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}/${COMPONENT_NAME}-config.cmake"
        "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}/${COMPONENT_NAME}-config-version.cmake"
      DESTINATION "${__atfw_pkg_config_INSTALL_DESTINATION}"
      COMPONENT ${COMPONENT_NAME})
  endif()
endfunction()

function(atframework_install_target TARGET_NAME)
  set(optionArgs NO_INSTALL_HEADERS)
  set(oneValueArgs COMPONENT_NAME INSTALL_DESTINATION INSTALL_HEADERS_DESTINATION INSTALL_RESOURCE_DESTINATION)
  set(multiValueArgs INSTALL_HEADER_DIRECTORY INSTALL_HEADER_FILES_MATCHING INSTALL_RESOURCE_DIRECTORY
                     INSTALL_RESOURCE_FILES_MATCHING)
  cmake_parse_arguments(__atfw_install_args "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT __atfw_install_args_COMPONENT_NAME)
    set(__atfw_install_args_COMPONENT_NAME "${TARGET_NAME}")
  endif()
  if(NOT __atfw_install_args_INSTALL_DESTINATION)
    set(__atfw_install_args_INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${ATFRAMEWORK_EXPORT_PACKAGE_NAME}")
  endif()

  if(SERVER_FRAME_VCS_COMMIT_SHORT_SHA)
    set(ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR
        "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}")
  else()
    set(ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
  endif()

  if(__atfw_install_args_INSTALL_HEADER_DIRECTORY AND NOT __atfw_install_args_NO_INSTALL_HEADERS)
    if(NOT __atfw_install_args_INSTALL_HEADERS_DESTINATION)
      set(__atfw_install_args_INSTALL_HEADERS_DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    endif()
    if(NOT __atfw_install_args_INSTALL_HEADER_FILES_MATCHING)
      set(__atfw_install_args_INSTALL_HEADER_FILES_MATCHING
          REGEX
          ".+\\.h(pp|xx)?$"
          PATTERN
          ".svn"
          EXCLUDE
          PATTERN
          ".git"
          EXCLUDE)
    endif()
    install(
      DIRECTORY ${__atfw_install_args_INSTALL_HEADER_DIRECTORY}
      DESTINATION "${__atfw_install_args_INSTALL_HEADERS_DESTINATION}"
      USE_SOURCE_PERMISSIONS
      COMPONENT "atframework::${__atfw_install_args_COMPONENT_NAME}"
      FILES_MATCHING ${__atfw_install_args_INSTALL_HEADER_FILES_MATCHING})
  endif()

  if(__atfw_install_args_INSTALL_RESOURCE_DIRECTORY AND __atfw_install_args_INSTALL_RESOURCE_DESTINATION)
    if(NOT __atfw_install_args_INSTALL_RESOURCE_FILES_MATCHING)
      set(__atfw_install_args_INSTALL_RESOURCE_FILES_MATCHING PATTERN ".svn" EXCLUDE PATTERN ".git" EXCLUDE)
    endif()
    install(
      DIRECTORY ${__atfw_install_args_INSTALL_RESOURCE_DIRECTORY}
      DESTINATION "${__atfw_install_args_INSTALL_RESOURCE_DESTINATION}"
      USE_SOURCE_PERMISSIONS
      COMPONENT "atframework::${__atfw_install_args_COMPONENT_NAME}"
      FILES_MATCHING ${__atfw_install_args_INSTALL_RESOURCE_FILES_MATCHING})
  endif()

  install(
    TARGETS ${TARGET_NAME}
    EXPORT "${ATFRAMEWORK_EXPORT_PACKAGE_NAME}-${__atfw_install_args_COMPONENT_NAME}-target"
    COMPONENT "atframework::${__atfw_install_args_COMPONENT_NAME}"
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION "${ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR}")

  install(
    EXPORT "${ATFRAMEWORK_EXPORT_PACKAGE_NAME}-${__atfw_install_args_COMPONENT_NAME}-target"
    FILE "${__atfw_install_args_COMPONENT_NAME}-target.cmake"
    NAMESPACE "atframework::"
    DESTINATION "${__atfw_install_args_INSTALL_DESTINATION}"
    COMPONENT "atframework::${__atfw_install_args_COMPONENT_NAME}")
endfunction()

function(atframework_install_files TARGET_NAME)
  set(optionArgs)
  set(oneValueArgs COMPONENT_NAME INSTALL_DESTINATION INSTALL_TYPE)
  set(multiValueArgs INSTALL_FILES)
  cmake_parse_arguments(__atfw_install_args "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT __atfw_install_args_COMPONENT_NAME)
    set(__atfw_install_args_COMPONENT_NAME "${TARGET_NAME}")
  endif()
  if(NOT __atfw_install_args_INSTALL_FILES)
    message(FATAL_ERROR "INSTALL_FILES is required")
  endif()
  if(__atfw_install_args_INSTALL_DESTINATION)
    set(__atfw_install_args_INSTALL_TO "DESTINATION" "${__atfw_install_args_INSTALL_DESTINATION}")
  elseif(__atfw_install_args_INSTALL_TYPE)
    set(__atfw_install_args_INSTALL_TO "TYPE" "${__atfw_install_args_INSTALL_TYPE}")
  else()
    message(FATAL_ERROR "INSTALL_DESTINATION or INSTALL_TYPE is required")
  endif()

  install(FILES ${__atfw_install_args_INSTALL_FILES} ${__atfw_install_args_INSTALL_TO}
          COMPONENT "atframework::${__atfw_install_args_COMPONENT_NAME}")
endfunction()

function(atframework_install_directories TARGET_NAME)
  set(optionArgs)
  set(oneValueArgs COMPONENT_NAME INSTALL_DESTINATION INSTALL_TYPE)
  set(multiValueArgs INSTALL_DIRECTORIES FILES_MATCHING)
  cmake_parse_arguments(__atfw_install_args "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT __atfw_install_args_COMPONENT_NAME)
    set(__atfw_install_args_COMPONENT_NAME "${TARGET_NAME}")
  endif()
  if(NOT __atfw_install_args_INSTALL_DIRECTORIES)
    message(FATAL_ERROR "INSTALL_DIRECTORIES is required")
  endif()
  if(__atfw_install_args_INSTALL_DESTINATION)
    set(__atfw_install_args_INSTALL_TO "DESTINATION" "${__atfw_install_args_INSTALL_DESTINATION}")
  elseif(__atfw_install_args_INSTALL_TYPE)
    set(__atfw_install_args_INSTALL_TO "TYPE" "${__atfw_install_args_INSTALL_TYPE}")
  else()
    message(FATAL_ERROR "INSTALL_DESTINATION or INSTALL_TYPE is required")
  endif()

  if(__atfw_install_args_FILES_MATCHING)
    install(
      DIRECTORY ${__atfw_install_args_INSTALL_DIRECTORIES} ${__atfw_install_args_INSTALL_TO}
      COMPONENT "atframework::${__atfw_install_args_COMPONENT_NAME}"
      USE_SOURCE_PERMISSIONS FILES_MATCHING ${__atfw_install_args_FILES_MATCHING})
  else()
    install(
      DIRECTORY ${__atfw_install_args_INSTALL_DIRECTORIES} ${__atfw_install_args_INSTALL_TO}
      COMPONENT "atframework::${__atfw_install_args_COMPONENT_NAME}"
      USE_SOURCE_PERMISSIONS)
  endif()
endfunction()

function(atframework_target_precompile_headers TARGET_NAME)
  if(PROJECT_ENABLE_PRECOMPILE_HEADERS AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(${TARGET_NAME} ${ARGN})
  endif()
endfunction()

function(atframework_add_library TARGET_NAME)
  set(optionArgs ENABLE_PUBLIC_PRECOMPILE_HEADERS ENABLE_PRIVATE_PRECOMPILE_HEADERS NO_INSTALL_HEADERS PROTOBUF_LIBRARY)
  set(oneValueArgs
      COMPONENT_NAME
      ROOT_DIR
      GENERATED_DIR
      PUBLIC_SYMBOL_DECL
      INSTALL_HEADERS_DESTINATION
      INSTALL_RESOURCE_DESTINATION
      FOLDER_PATH)
  set(multiValueArgs
      HEADERS
      SOURCES
      GENERATED_HEADERS
      GENERATED_SOURCES
      PUBLIC_LINK_NAMES
      PRIVATE_LINK_NAMES
      PUBLIC_INCLUDE_DIRECTORY
      PRIVATE_INCLUDE_DIRECTORY
      INSTALL_HEADER_DIRECTORY
      INSTALL_HEADER_FILES_MATCHING
      INSTALL_RESOURCE_DIRECTORY
      INSTALL_RESOURCE_FILES_MATCHING)
  cmake_parse_arguments(__atfw_add_library_args "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT __atfw_add_library_args_COMPONENT_NAME)
    set(__atfw_add_library_args_COMPONENT_NAME "${TARGET_NAME}")
  endif()

  if(NOT __atfw_add_library_args_ROOT_DIR)
    set(__atfw_add_library_args_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  if(__atfw_add_library_args_HEADERS OR __atfw_add_library_args_SOURCES)
    if(NOT __atfw_add_library_args_ROOT_DIR)
      set(__atfw_add_library_args_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    source_group(TREE "${__atfw_add_library_args_ROOT_DIR}" FILES ${__atfw_add_library_args_HEADERS}
                                                                  ${__atfw_add_library_args_SOURCES})
  endif()

  if(__atfw_add_library_args_GENERATED_HEADERS OR __atfw_add_library_args_GENERATED_SOURCES)
    if(NOT __atfw_add_library_args_GENERATED_DIR)
      if(NOT PROJECT_GENERATED_DIR)
        set(PROJECT_GENERATED_DIR "${CMAKE_BINARY_DIR}/_generated")
      endif()
      set(__atfw_add_library_args_GENERATED_DIR "${PROJECT_GENERATED_DIR}")
    endif()
    source_group(TREE "${__atfw_add_library_args_GENERATED_DIR}" FILES ${__atfw_add_library_args_GENERATED_HEADERS}
                                                                       ${__atfw_add_library_args_GENERATED_SOURCES})
  endif()

  set(__atfw_add_library_is_interface)
  if(NOT __atfw_add_library_args_SOURCES AND NOT __atfw_add_library_args_GENERATED_SOURCES)
    add_library(${TARGET_NAME} INTERFACE)
    set(__atfw_add_library_is_interface TRUE)
  elseif(BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
    add_library(
      ${TARGET_NAME} SHARED ${__atfw_add_library_args_HEADERS} ${__atfw_add_library_args_SOURCES}
                            ${__atfw_add_library_args_GENERATED_HEADERS} ${__atfw_add_library_args_GENERATED_SOURCES})
    if(__atfw_add_library_args_PUBLIC_SYMBOL_DECL)
      project_build_tools_set_shared_library_declaration(${__atfw_add_library_args_PUBLIC_SYMBOL_DECL} ${TARGET_NAME})
    endif()
    set(__atfw_add_library_is_interface FALSE)
    set_target_properties(${TARGET_NAME} PROPERTIES ENABLE_EXPORTS TRUE)
  else()
    add_library(
      ${TARGET_NAME} STATIC ${__atfw_add_library_args_HEADERS} ${__atfw_add_library_args_SOURCES}
                            ${__atfw_add_library_args_GENERATED_HEADERS} ${__atfw_add_library_args_GENERATED_SOURCES})
    if(__atfw_add_library_args_PUBLIC_SYMBOL_DECL)
      project_build_tools_set_static_library_declaration(${__atfw_add_library_args_PUBLIC_SYMBOL_DECL} ${TARGET_NAME})
    endif()
    set(__atfw_add_library_is_interface FALSE)
  endif()
  if(__atfw_add_library_args_FOLDER_PATH)
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "${__atfw_add_library_args_FOLDER_PATH}")
  else()
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "atframework/library")
  endif()

  if(__atfw_add_library_is_interface)
    if(__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY)
      target_include_directories(
        ${TARGET_NAME} INTERFACE "$<BUILD_INTERFACE:${__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY}>"
                                 "$<INSTALL_INTERFACE:include>")
    endif()
    if(__atfw_add_library_args_PUBLIC_LINK_NAMES)
      target_link_libraries(${TARGET_NAME} INTERFACE ${__atfw_add_library_args_PUBLIC_LINK_NAMES})
    endif()
    if(__atfw_add_library_args_ENABLE_PUBLIC_PRECOMPILE_HEADERS AND (__atfw_add_library_args_HEADERS
                                                                     OR __atfw_add_library_args_GENERATED_HEADERS))
      atframework_target_precompile_headers(
        ${TARGET_NAME} INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__atfw_add_library_args_HEADERS}>>"
        "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__atfw_add_library_args_GENERATED_HEADERS}>>")
    endif()
  else()
    if(__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY)
      target_include_directories(
        ${TARGET_NAME} PUBLIC "$<BUILD_INTERFACE:${__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY}>"
                              "$<INSTALL_INTERFACE:include>")
    endif()
    if(__atfw_add_library_args_PRIVATE_INCLUDE_DIRECTORY)
      target_include_directories(${TARGET_NAME} PRIVATE "${__atfw_add_library_args_PRIVATE_INCLUDE_DIRECTORY}")
    endif()
    if(__atfw_add_library_args_PUBLIC_LINK_NAMES)
      target_link_libraries(${TARGET_NAME} PUBLIC ${__atfw_add_library_args_PUBLIC_LINK_NAMES})
    endif()
    if(__atfw_add_library_args_PRIVATE_LINK_NAMES)
      target_link_libraries(${TARGET_NAME} PRIVATE ${__atfw_add_library_args_PRIVATE_LINK_NAMES})
    endif()

    # RPATH
    if(SERVER_FRAME_VCS_COMMIT_SHORT_SHA)
      set(TARGET_INSTALL_RPATH
          "${PROJECT_RPATH_ORIGIN}"
          "${PROJECT_RPATH_ORIGIN}/../../${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${TARGET_NAME}/${CMAKE_INSTALL_LIBDIR}"
          ${PROJECT_INSTALL_RPATH}
          "${PROJECT_RPATH_ORIGIN}/../../../${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/__shared/${CMAKE_INSTALL_LIBDIR}"
          ${PROJECT_EXTERNAL_RPATH})
    else()
      set(TARGET_INSTALL_RPATH "${PROJECT_RPATH_ORIGIN}" ${PROJECT_INSTALL_RPATH} ${PROJECT_EXTERNAL_RPATH})
    endif()
    set_target_properties(
      ${TARGET_NAME}
      PROPERTIES C_VISIBILITY_PRESET "hidden"
                 CXX_VISIBILITY_PRESET "hidden"
                 VERSION "${PROJECT_VERSION}"
                 SOVERSION "${PROJECT_VERSION}"
                 BUILD_RPATH_USE_ORIGIN YES
                 INSTALL_RPATH "${TARGET_INSTALL_RPATH}")
    if(__atfw_add_library_args_PROTOBUF_LIBRARY)
      set_target_properties(${TARGET_NAME} PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "" CXX_CLANG_TIDY "")
      target_compile_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PROTOCOL_SOURCE_COMPILE_OPTIONS})
    else()
      target_compile_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
    endif()
    if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
      target_link_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
    endif()
    if(__atfw_add_library_args_PROTOBUF_LIBRARY)
      if(__atfw_add_library_is_interface)
        target_link_libraries(${TARGET_NAME} INTERFACE ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME})
      else()
        target_link_libraries(${TARGET_NAME} PUBLIC ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME})
      endif()
    endif()

    if(__atfw_add_library_args_ENABLE_PUBLIC_PRECOMPILE_HEADERS AND (__atfw_add_library_args_HEADERS
                                                                     OR __atfw_add_library_args_GENERATED_HEADERS))
      atframework_target_precompile_headers(
        ${TARGET_NAME} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__atfw_add_library_args_HEADERS}>>"
        "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__atfw_add_library_args_GENERATED_HEADERS}>>")
    elseif(__atfw_add_library_args_ENABLE_PRIVATE_PRECOMPILE_HEADERS AND (__atfw_add_library_args_HEADERS
                                                                          OR __atfw_add_library_args_GENERATED_HEADERS))
      atframework_target_precompile_headers(
        ${TARGET_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${__atfw_add_library_args_HEADERS}>"
        "$<$<COMPILE_LANGUAGE:CXX>:${__atfw_add_library_args_GENERATED_HEADERS}>")
    endif()
  endif()

  add_library(atframework::${TARGET_NAME} ALIAS ${TARGET_NAME})

  # Install target
  set(__atfw_install_args ${TARGET_NAME} COMPONENT_NAME ${__atfw_add_library_args_COMPONENT_NAME})
  set(__atfw_install_forward_args NO_INSTALL_HEADERS INSTALL_RESOURCE_DESTINATION INSTALL_RESOURCE_DIRECTORY
                                  INSTALL_RESOURCE_FILES_MATCHING)
  if(NOT __atfw_add_library_args_NO_INSTALL_HEADERS)
    list(APPEND __atfw_install_forward_args INSTALL_HEADERS_DESTINATION INSTALL_HEADER_DIRECTORY
         INSTALL_HEADER_FILES_MATCHING)
  endif()
  foreach(_forward_arg IN LISTS __atfw_install_forward_args)
    if(__atfw_add_library_args_${_forward_arg})
      list(APPEND __atfw_install_args ${_forward_arg} ${__atfw_add_library_args_${_forward_arg}})
    endif()
  endforeach()

  atframework_install_target(${__atfw_install_args})

  # Install generated headers
  if(NOT __atfw_add_library_args_NO_INSTALL_HEADERS
     AND __atfw_add_library_args_GENERATED_HEADERS
     AND __atfw_add_library_args_GENERATED_DIR)
    if(NOT __atfw_add_library_args_INSTALL_HEADERS_DESTINATION)
      set(__atfw_add_library_args_INSTALL_HEADERS_DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    endif()
    if(NOT __atfw_add_library_args_INSTALL_HEADER_FILES_MATCHING)
      set(__atfw_add_library_args_INSTALL_HEADER_FILES_MATCHING
          REGEX
          ".+\\.h(pp|xx)?$"
          PATTERN
          ".svn"
          EXCLUDE
          PATTERN
          ".git"
          EXCLUDE)
    endif()
    atframework_install_directories(
      ${TARGET_NAME}
      INSTALL_DESTINATION
      "${__atfw_add_library_args_INSTALL_HEADERS_DESTINATION}"
      INSTALL_DIRECTORIES
      ${__atfw_add_library_args_GENERATED_DIR}
      FILES_MATCHING
      ${__atfw_add_library_args_INSTALL_HEADER_FILES_MATCHING})
  endif()
endfunction()

function(atframework_add_executable TARGET_NAME)
  set(optionArgs ENABLE_PUBLIC_PRECOMPILE_HEADERS ENABLE_PRIVATE_PRECOMPILE_HEADERS)
  set(oneValueArgs
      COMPONENT_NAME
      ROOT_DIR
      GENERATED_DIR
      PUBLIC_SYMBOL_DECL
      INSTALL_RESOURCE_DESTINATION
      FOLDER_PATH
      RUNTIME_OUTPUT_DIRECTORY)
  set(multiValueArgs
      HEADERS
      SOURCES
      GENERATED_HEADERS
      GENERATED_SOURCES
      PUBLIC_LINK_NAMES
      PRIVATE_LINK_NAMES
      PUBLIC_INCLUDE_DIRECTORY
      PRIVATE_INCLUDE_DIRECTORY
      INSTALL_RESOURCE_DIRECTORY
      INSTALL_RESOURCE_FILES_MATCHING)
  cmake_parse_arguments(__atfw_add_library_args "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT __atfw_add_library_args_COMPONENT_NAME)
    set(__atfw_add_library_args_COMPONENT_NAME "${TARGET_NAME}")
  endif()

  if(NOT __atfw_add_library_args_ROOT_DIR)
    set(__atfw_add_library_args_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  if(NOT __atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY)
    set(__atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/atframework/${TARGET_NAME}/bin")
  endif()

  if(__atfw_add_library_args_HEADERS OR __atfw_add_library_args_SOURCES)
    if(NOT __atfw_add_library_args_ROOT_DIR)
      set(__atfw_add_library_args_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    source_group(TREE "${__atfw_add_library_args_ROOT_DIR}" FILES ${__atfw_add_library_args_HEADERS}
                                                                  ${__atfw_add_library_args_SOURCES})
  endif()

  if(__atfw_add_library_args_GENERATED_HEADERS OR __atfw_add_library_args_GENERATED_SOURCES)
    if(NOT __atfw_add_library_args_GENERATED_DIR)
      if(NOT PROJECT_GENERATED_DIR)
        set(PROJECT_GENERATED_DIR "${CMAKE_BINARY_DIR}/_generated")
      endif()
      set(__atfw_add_library_args_GENERATED_DIR "${PROJECT_GENERATED_DIR}")
    endif()
    source_group(TREE "${__atfw_add_library_args_GENERATED_DIR}" FILES ${__atfw_add_library_args_GENERATED_HEADERS}
                                                                       ${__atfw_add_library_args_GENERATED_SOURCES})
  endif()

  add_executable(
    ${TARGET_NAME} ${__atfw_add_library_args_HEADERS} ${__atfw_add_library_args_SOURCES}
                   ${__atfw_add_library_args_GENERATED_HEADERS} ${__atfw_add_library_args_GENERATED_SOURCES})
  if(__atfw_add_library_args_PUBLIC_SYMBOL_DECL)
    project_build_tools_set_static_library_declaration(${__atfw_add_library_args_PUBLIC_SYMBOL_DECL} ${TARGET_NAME})
  endif()

  if(__atfw_add_library_args_FOLDER_PATH)
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "${__atfw_add_library_args_FOLDER_PATH}")
  else()
    set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "atframework/service")
  endif()

  if(__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY)
    target_include_directories(
      ${TARGET_NAME} PUBLIC "$<BUILD_INTERFACE:${__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY}>"
                            "$<INSTALL_INTERFACE:include>")
  endif()
  if(__atfw_add_library_args_PRIVATE_INCLUDE_DIRECTORY)
    target_include_directories(${TARGET_NAME} PRIVATE "${__atfw_add_library_args_PRIVATE_INCLUDE_DIRECTORY}")
  endif()
  if(__atfw_add_library_args_PUBLIC_LINK_NAMES)
    target_link_libraries(${TARGET_NAME} PUBLIC ${__atfw_add_library_args_PUBLIC_LINK_NAMES})
  endif()
  if(__atfw_add_library_args_PRIVATE_LINK_NAMES)
    target_link_libraries(${TARGET_NAME} PRIVATE ${__atfw_add_library_args_PRIVATE_LINK_NAMES})
  endif()

  # RPATH
  if(SERVER_FRAME_VCS_COMMIT_SHORT_SHA)
    set(TARGET_INSTALL_RPATH
        "${PROJECT_RPATH_ORIGIN}"
        "${PROJECT_RPATH_ORIGIN}/../../${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${TARGET_NAME}/${CMAKE_INSTALL_LIBDIR}"
        ${PROJECT_INSTALL_RPATH}
        "${PROJECT_RPATH_ORIGIN}/../../../${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/__shared/${CMAKE_INSTALL_LIBDIR}"
        ${PROJECT_EXTERNAL_RPATH})
  else()
    set(TARGET_INSTALL_RPATH "${PROJECT_RPATH_ORIGIN}" ${PROJECT_INSTALL_RPATH} ${PROJECT_EXTERNAL_RPATH})
  endif()
  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES C_VISIBILITY_PRESET "hidden"
               CXX_VISIBILITY_PRESET "hidden"
               VERSION "${PROJECT_VERSION}"
               RUNTIME_OUTPUT_DIRECTORY "${__atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY}"
               BUILD_RPATH_USE_ORIGIN YES
               INSTALL_RPATH "${TARGET_INSTALL_RPATH}")
  target_compile_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
  if(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
    target_link_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_LINK_OPTIONS})
  endif()

  if(__atfw_add_library_args_ENABLE_PUBLIC_PRECOMPILE_HEADERS AND (__atfw_add_library_args_HEADERS
                                                                   OR __atfw_add_library_args_GENERATED_HEADERS))
    atframework_target_precompile_headers(
      ${TARGET_NAME} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__atfw_add_library_args_HEADERS}>>"
      "$<$<COMPILE_LANGUAGE:CXX>:$<BUILD_INTERFACE:${__atfw_add_library_args_GENERATED_HEADERS}>>")
  elseif(__atfw_add_library_args_ENABLE_PRIVATE_PRECOMPILE_HEADERS AND (__atfw_add_library_args_HEADERS
                                                                        OR __atfw_add_library_args_GENERATED_HEADERS))
    atframework_target_precompile_headers(
      ${TARGET_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${__atfw_add_library_args_HEADERS}>"
      "$<$<COMPILE_LANGUAGE:CXX>:${__atfw_add_library_args_GENERATED_HEADERS}>")
  endif()

  add_executable(atframework::${TARGET_NAME} ALIAS ${TARGET_NAME})

  # Install target
  set(__atfw_install_args ${TARGET_NAME} COMPONENT_NAME ${__atfw_add_library_args_COMPONENT_NAME})
  set(__atfw_install_forward_args INSTALL_RESOURCE_DESTINATION INSTALL_RESOURCE_DIRECTORY
                                  INSTALL_RESOURCE_FILES_MATCHING)
  foreach(_forward_arg IN LISTS __atfw_install_forward_args)
    if(__atfw_add_library_args_${_forward_arg})
      list(APPEND __atfw_install_args ${_forward_arg} ${__atfw_add_library_args_${_forward_arg}})
    endif()
  endforeach()

  atframework_install_target(${__atfw_install_args})
endfunction()

function(atframework_add_protobuf_library TARGET_NAME PROTOCOL_DIR)
  set(optionArgs NO_INSTALL_PBFILE NO_INSTALL_HEADERS ENABLE_PUBLIC_PRECOMPILE_HEADERS
                 ENABLE_PRIVATE_PRECOMPILE_HEADERS)
  set(oneValueArgs
      OUTPUT_DIR
      PUBLIC_SYMBOL_DECL
      OUTPUT_PBFILE_PATH
      OUTPUT_HEADERS
      OUTPUT_SOURCES
      FOLDER_PATH
      COMPONENT_NAME)
  set(multiValueArgs PROTOCOLS PUBLIC_LINK_NAMES PRIVATE_LINK_NAMES PUBLIC_INCLUDE_DIRECTORY PRIVATE_INCLUDE_DIRECTORY)
  cmake_parse_arguments(__atfw_add_library_args "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT __atfw_add_library_args_OUTPUT_DIR)
    set(__atfw_add_library_args_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/_generated")
  endif()
  if(NOT __atfw_add_library_args_PROTOCOLS)
    message(FATAL_ERROR "PROTOCOLS is required for atframework_add_protobuf_protolib")
  endif()
  if(NOT __atfw_add_library_args_FOLDER_PATH)
    set(__atfw_add_library_args_FOLDER_PATH "atframework/protocol/${TARGET_NAME}")
  endif()
  if(NOT __atfw_add_library_args_COMPONENT_NAME)
    set(__atfw_add_library_args_COMPONENT_NAME "${TARGET_NAME}")
  endif()
  echowithcolor(COLOR GREEN "-- Configure ${TARGET_NAME} on ${PROTOCOL_DIR}")

  file(MAKE_DIRECTORY "${__atfw_add_library_args_OUTPUT_DIR}/src")
  file(MAKE_DIRECTORY "${__atfw_add_library_args_OUTPUT_DIR}/temp")
  file(MAKE_DIRECTORY "${__atfw_add_library_args_OUTPUT_DIR}/pb")

  set(__atfw_generated_pbfile "${__atfw_add_library_args_OUTPUT_DIR}/pb/${TARGET_NAME}.pb")
  set(__atfw_generated_headers)
  set(__atfw_generated_sources)
  set(__atfw_generated_relative_files)
  set(__atfw_generated_pch_headers)
  set(__atfw_generated_last_created_dir ".")
  set(__atfw_generated_copy_commands)
  set(__atfw_add_library_options PROTOBUF_LIBRARY)
  if(__atfw_add_library_args_ENABLE_PUBLIC_PRECOMPILE_HEADERS)
    list(APPEND __atfw_add_library_options ENABLE_PUBLIC_PRECOMPILE_HEADERS)
  endif()
  if(__atfw_add_library_args_ENABLE_PRIVATE_PRECOMPILE_HEADERS)
    list(APPEND __atfw_add_library_options ENABLE_PRIVATE_PRECOMPILE_HEADERS)
  endif()
  if(__atfw_add_library_args_NO_INSTALL_HEADERS)
    list(APPEND __atfw_add_library_options NO_INSTALL_HEADERS)
  endif()
  if(__atfw_add_library_args_PUBLIC_LINK_NAMES)
    list(APPEND __atfw_add_library_options PUBLIC_LINK_NAMES ${__atfw_add_library_args_PUBLIC_LINK_NAMES})
  endif()
  if(__atfw_add_library_args_PRIVATE_LINK_NAMES)
    list(APPEND __atfw_add_library_options PRIVATE_LINK_NAMES ${__atfw_add_library_args_PRIVATE_LINK_NAMES})
  endif()
  list(APPEND __atfw_add_library_options PUBLIC_INCLUDE_DIRECTORY "${__atfw_add_library_args_OUTPUT_DIR}/src")
  if(__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY)
    list(APPEND __atfw_add_library_options ${__atfw_add_library_args_PUBLIC_INCLUDE_DIRECTORY})
  endif()
  if(__atfw_add_library_args_PRIVATE_INCLUDE_DIRECTORY)
    list(APPEND __atfw_add_library_options PRIVATE_INCLUDE_DIRECTORY
         ${__atfw_add_library_args_PRIVATE_INCLUDE_DIRECTORY})
  endif()

  list(SORT __atfw_add_library_args_PROTOCOLS)
  foreach(PROTO_FILE ${__atfw_add_library_args_PROTOCOLS})
    file(RELATIVE_PATH RELATIVE_FILE_PATH "${PROTOCOL_DIR}" "${PROTO_FILE}")
    string(REGEX REPLACE "\\.proto$" "" RELATIVE_FILE_PREFIX "${RELATIVE_FILE_PATH}")
    list(APPEND __atfw_generated_headers "${__atfw_add_library_args_OUTPUT_DIR}/src/${RELATIVE_FILE_PREFIX}.pb.h")
    list(APPEND __atfw_generated_pch_headers "\"${RELATIVE_FILE_PREFIX}.pb.h\"")
    list(APPEND __atfw_generated_sources "${__atfw_add_library_args_OUTPUT_DIR}/src/${RELATIVE_FILE_PREFIX}.pb.cc")
    list(APPEND __atfw_generated_relative_files "src/${RELATIVE_FILE_PREFIX}.pb.h" "src/${RELATIVE_FILE_PREFIX}.pb.cc")
    get_filename_component(__atfw_final_generated_source_dir
                           "${__atfw_add_library_args_OUTPUT_DIR}/src/${RELATIVE_FILE_PREFIX}.pb.cc" DIRECTORY)
    if(NOT __atfw_generated_last_created_dir STREQUAL __atfw_final_generated_source_dir)
      if(NOT EXISTS "${__atfw_final_generated_source_dir}")
        file(MAKE_DIRECTORY "${__atfw_final_generated_source_dir}")
      endif()
      set(__atfw_generated_last_created_dir "${__atfw_final_generated_source_dir}")

      if(__atfw_generated_copy_commands)
        list(APPEND __atfw_generated_copy_commands "${__atfw_generated_last_created_dir}")
      endif()
      list(
        APPEND
        __atfw_generated_copy_commands
        "COMMAND"
        "${CMAKE_COMMAND}"
        "-E"
        "copy_if_different"
        "${__atfw_add_library_args_OUTPUT_DIR}/temp/${RELATIVE_FILE_PREFIX}.pb.h"
        "${__atfw_add_library_args_OUTPUT_DIR}/temp/${RELATIVE_FILE_PREFIX}.pb.cc")
    else()
      list(APPEND __atfw_generated_copy_commands
           "${__atfw_add_library_args_OUTPUT_DIR}/temp/${RELATIVE_FILE_PREFIX}.pb.h"
           "${__atfw_add_library_args_OUTPUT_DIR}/temp/${RELATIVE_FILE_PREFIX}.pb.cc")
    endif()
  endforeach()
  if(__atfw_generated_copy_commands)
    list(APPEND __atfw_generated_copy_commands "${__atfw_generated_last_created_dir}")
  endif()

  if(VCPKG_INSTALLED_DIR
     AND VCPKG_TARGET_TRIPLET
     AND EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/google/protobuf/descriptor.proto")
    set(PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
  else()
    set(PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR "${PROJECT_THIRD_PARTY_INSTALL_DIR}/include")
  endif()

  set(__atfw_proto_paths
      --proto_path
      "${PROTOCOL_DIR}"
      --proto_path
      "${PROJECT_THIRD_PARTY_PROTOBUF_PROTO_DIR}"
      --proto_path
      "${ATFRAMEWORK_LIBATBUS_REPO_DIR}/include"
      --proto_path
      "${ATFRAMEWORK_LIBATAPP_REPO_DIR}/include")

  if(NOT __atfw_add_library_args_PUBLIC_SYMBOL_DECL)
    string(REGEX REPLACE "[-\\.]" "_" __atfw_add_library_args_PUBLIC_SYMBOL_DECL "${TARGET_NAME}")
    string(REGEX REPLACE "[\\\$\\\\/]" "" __atfw_add_library_args_PUBLIC_SYMBOL_DECL
                         "${__atfw_add_library_args_PUBLIC_SYMBOL_DECL}")
    string(REPLACE "::" "_" __atfw_add_library_args_PUBLIC_SYMBOL_DECL
                   "${__atfw_add_library_args_PUBLIC_SYMBOL_DECL}_API")
    string(TOUPPER "${__atfw_add_library_args_PUBLIC_SYMBOL_DECL}" __atfw_add_library_args_PUBLIC_SYMBOL_DECL)
  endif()

  add_custom_command(
    OUTPUT ${__atfw_generated_sources} ${__atfw_generated_headers} "${__atfw_generated_pbfile}"
    COMMAND
      "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}" ${__atfw_proto_paths} --cpp_out
      "dllexport_decl=${__atfw_add_library_args_PUBLIC_SYMBOL_DECL}:${__atfw_add_library_args_OUTPUT_DIR}/temp" -o
      "${__atfw_add_library_args_OUTPUT_DIR}/temp/${TARGET_NAME}.pb"
      # Protocol buffer files
      ${__atfw_add_library_args_PROTOCOLS} ${__atfw_generated_copy_commands}
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_THIRD_PARTY_ROOT_DIR}/.clang-tidy"
            "${__atfw_add_library_args_OUTPUT_DIR}/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${__atfw_add_library_args_OUTPUT_DIR}/temp/${TARGET_NAME}.pb"
            "${__atfw_add_library_args_OUTPUT_DIR}/pb/"
    WORKING_DIRECTORY "${__atfw_add_library_args_OUTPUT_DIR}"
    DEPENDS ${__atfw_add_library_args_PROTOCOLS} "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_BIN_PROTOC}"
    COMMENT "Generate [@${__atfw_add_library_args_OUTPUT_DIR}] ${__atfw_generated_relative_files}")

  atframework_add_library(
    "${TARGET_NAME}"
    ${__atfw_add_library_options}
    COMPONENT_NAME
    "${__atfw_add_library_args_COMPONENT_NAME}"
    ROOT_DIR
    "${__atfw_add_library_args_OUTPUT_DIR}/src"
    GENERATED_DIR
    "${__atfw_add_library_args_OUTPUT_DIR}/src"
    PUBLIC_SYMBOL_DECL
    "${__atfw_add_library_args_PUBLIC_SYMBOL_DECL}"
    FOLDER_PATH
    "${__atfw_add_library_args_FOLDER_PATH}"
    GENERATED_HEADERS
    ${__atfw_generated_headers}
    GENERATED_SOURCES
    ${__atfw_generated_sources}
    PUBLIC_LINK_NAMES
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME})

  if(NOT __atfw_add_library_args_NO_INSTALL_PBFILE)
    atframework_install_files(
      "${TARGET_NAME}"
      COMPONENT_NAME
      "${__atfw_add_library_args_COMPONENT_NAME}"
      INSTALL_DESTINATION
      "${PROJECT_INSTALL_RES_PBD_DIR}"
      INSTALL_FILES
      "${__atfw_generated_pbfile}")
  endif()

  if(__atfw_add_library_args_OUTPUT_PBFILE_PATH)
    set(${__atfw_add_library_args_OUTPUT_PBFILE_PATH}
        "${__atfw_generated_pbfile}"
        PARENT_SCOPE)
  endif()
  if(__atfw_add_library_args_OUTPUT_HEADERS)
    set(${__atfw_add_library_args_OUTPUT_HEADERS}
        ${__atfw_generated_headers}
        PARENT_SCOPE)
  endif()
  if(__atfw_add_library_args_OUTPUT_SOURCES)
    set(${__atfw_add_library_args_OUTPUT_SOURCES}
        ${__atfw_generated_sources}
        PARENT_SCOPE)
  endif()
endfunction()
