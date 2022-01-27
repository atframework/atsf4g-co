function(project_service_declare_instance TARGET_NAME SERVICE_ROOT_DIR)
  set(optionArgs "")
  set(oneValueArgs INCLUDE_DIR OUTPUT_NAME OUTPUT_TARGET_NAME RUNTIME_OUTPUT_DIRECTORY)
  set(multiValueArgs HRADERS SOURCES RESOURCE_DIRECTORIES RESOURCE_FILES USE_COMPONENTS)
  cmake_parse_arguments(project_service_declare_instance "${optionArgs}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  echowithcolor(COLOR GREEN "-- Configure service ${TARGET_NAME} on ${SERVICE_ROOT_DIR}")

  source_group_by_dir(project_service_declare_instance_HRADERS project_service_declare_instance_SOURCES)
  add_executable(${TARGET_NAME} ${project_service_declare_instance_HRADERS} ${project_service_declare_instance_SOURCES})

  target_compile_options(${TARGET_NAME} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})
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
  list(APPEND LINK_TARGETS ${PROJECT_SERVER_FRAME_LIB_LINK})
  target_link_libraries(${TARGET_NAME} PRIVATE ${LINK_TARGETS})

  if(MSVC)
    set_property(TARGET "${TARGET_NAME}" PROPERTY FOLDER "service")
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
