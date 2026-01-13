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
  set(optionArgs)
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

  if(__atfw_install_args_INSTALL_HEADER_DIRECTORY)
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
      COMPONENT ${__atfw_install_args_COMPONENT_NAME}
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
      COMPONENT ${__atfw_install_args_COMPONENT_NAME}
      FILES_MATCHING ${__atfw_install_args_INSTALL_HEADER_FILES_MATCHING})
  endif()

  install(
    TARGETS ${TARGET_NAME}
    EXPORT "${ATFRAMEWORK_EXPORT_PACKAGE_NAME}-${__atfw_install_args_COMPONENT_NAME}-target"
    COMPONENT ${__atfw_install_args_COMPONENT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION "${ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${ATFRAMEWORK_EXPORT_CMAKE_INSTALL_LIBDIR}")

  install(
    EXPORT "${ATFRAMEWORK_EXPORT_PACKAGE_NAME}-${__atfw_install_args_COMPONENT_NAME}-target"
    FILE "${__atfw_install_args_COMPONENT_NAME}-target.cmake"
    NAMESPACE "atframework::"
    DESTINATION "${__atfw_install_args_INSTALL_DESTINATION}"
    COMPONENT ${__atfw_install_args_COMPONENT_NAME})
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
          COMPONENT "${__atfw_install_args_COMPONENT_NAME}")
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
      COMPONENT "${__atfw_install_args_COMPONENT_NAME}"
      USE_SOURCE_PERMISSIONS FILES_MATCHING ${__atfw_install_args_FILES_MATCHING})
  else()
    install(
      DIRECTORY ${__atfw_install_args_INSTALL_DIRECTORIES} ${__atfw_install_args_INSTALL_TO}
      COMPONENT "${__atfw_install_args_COMPONENT_NAME}"
      USE_SOURCE_PERMISSIONS)
  endif()
endfunction()

function(atframework_target_precompile_headers TARGET_NAME)
  if(PROJECT_ENABLE_PRECOMPILE_HEADERS AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(${TARGET_NAME} ${ARGN})
  endif()
endfunction()

function(atframework_add_library TARGET_NAME)
  set(optionArgs ENABLE_PUBLIC_PRECOMPILE_HEADERS ENABLE_PRIVATE_PRECOMPILE_HEADERS)
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
  endif()

  add_library(atframework::${TARGET_NAME} ALIAS ${TARGET_NAME})

  # Install target
  set(__atfw_install_args ${TARGET_NAME} COMPONENT_NAME ${__atfw_add_library_args_COMPONENT_NAME})
  set(__atfw_install_forward_args
      INSTALL_HEADERS_DESTINATION INSTALL_HEADER_DIRECTORY INSTALL_HEADER_FILES_MATCHING INSTALL_RESOURCE_DESTINATION
      INSTALL_RESOURCE_DIRECTORY INSTALL_RESOURCE_FILES_MATCHING)
  foreach(_forward_arg IN LISTS __atfw_install_forward_args)
    if(__atfw_add_library_args_${_forward_arg})
      list(APPEND __atfw_install_args ${_forward_arg} ${__atfw_add_library_args_${_forward_arg}})
    endif()
  endforeach()

  atframework_install_target(${__atfw_install_args})

  # Install generated headers
  if(__atfw_add_library_args_GENERATED_HEADERS AND __atfw_add_library_args_GENERATED_DIR)
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
    set(__atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${TARGET_NAME}/bin")
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

  # 针对MSVC多配置生成器，防止自动添加Debug目录
  if(MSVC)
    set_target_properties(${TARGET_NAME} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${__atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY}"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${__atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY}"
      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${__atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY}"
      RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${__atfw_add_library_args_RUNTIME_OUTPUT_DIRECTORY}")
  endif()

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
