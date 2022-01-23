set(PROJECT_INSTALL_COMPONENT_EXPORT_NAME "${PROJECT_NAME}-component-target")
set(PROJECT_INSTALL_COMPONENT_EXPORT_FILE
    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/${PROJECT_INSTALL_COMPONENT_EXPORT_NAME}.cmake")

function(project_component_declare_sdk SDK_NAME)
  set(optionArgs "")
  set(oneValueArgs INCLUDE_DIR OUTPUT_NAME)
  set(multiValueArgs HRADERS SOURCES)
  cmake_parse_arguments(project_component_declare_sdk "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(TARGET_NAME "${PROJECT_NAME}-component-${SDK_NAME}")
  if(project_component_declare_sdk_SOURCES)
    if(NOT WIN32 AND (BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY))
      add_library(${TARGET_NAME} SHARED ${project_component_declare_sdk_HRADERS}
                                        ${project_component_declare_sdk_SOURCES})
    else()
      add_library(${TARGET_NAME} STATIC ${project_component_declare_sdk_HRADERS}
                                        ${project_component_declare_sdk_SOURCES})
    endif()
  else()
    add_library(${TARGET_NAME} INTERFACE)
  endif()
  if(project_component_declare_sdk_OUTPUT_NAME)
    set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${project_component_declare_sdk_OUTPUT_NAME}")
  endif()

  if(project_component_declare_sdk_HRADERS AND project_component_declare_sdk_INCLUDE_DIR)
    if(project_component_declare_sdk_SOURCES)
      target_include_directories(${TARGET_NAME}
                                 PUBLIC "$<BUILD_INTERFACE:${project_component_declare_sdk_INCLUDE_DIR}>")
    else()
      target_include_directories(${TARGET_NAME}
                                 INTERFACE "$<BUILD_INTERFACE:${project_component_declare_sdk_INCLUDE_DIR}>")
    endif()
  endif()

  if(project_component_declare_sdk_SOURCES)
    target_link_libraries(${TARGET_NAME} PUBLIC ${PROJECT_SERVER_FRAME_LIB_LINK})
  elseif(project_component_declare_sdk_HRADERS)
    target_link_libraries(${TARGET_NAME} INTERFACE ${PROJECT_SERVER_FRAME_LIB_LINK})
  endif()

  install(
    TARGETS ${TARGET_NAME}
    EXPORT ${PROJECT_INSTALL_COMPONENT_EXPORT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

  if(project_component_declare_sdk_HRADERS AND project_component_declare_sdk_INCLUDE_DIR)
    install(
      DIRECTORY ${project_component_declare_sdk_INCLUDE_DIR}
      TYPE INCLUDE
      USE_SOURCE_PERMISSIONS FILES_MATCHING
      REGEX ".+\\.h(pp)?$"
      PATTERN ".svn" EXCLUDE
      PATTERN ".git" EXCLUDE)
  endif()

  add_library("components::${SDK_NAME}" ALIAS "${TARGET_NAME}")
endfunction()
