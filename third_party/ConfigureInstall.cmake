# Copy dynamic libraries and executables
set(PROJECT_COPY_LIBRARY_RULES
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib/*.so"
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib/*.so.*"
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib/*.dll"
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib/*.dll.*"
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib64/*.so"
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib64/*.so.*"
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib64/*.dll"
    "${PROJECT_THIRD_PARTY_INSTALL_DIR}/lib64/*.dll.*")
file(GLOB PROJECT_COPY_LIBRARIES ${PROJECT_COPY_LIBRARY_RULES})
project_link_or_copy_files(${PROJECT_COPY_LIBRARIES} "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

file(GLOB PROJECT_COPY_EXECUTABLES "${PROJECT_THIRD_PARTY_INSTALL_DIR}/bin/*")
project_link_or_copy_files(${PROJECT_COPY_EXECUTABLES} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

unset(PROJECT_COPY_LIBRARY_RULES)
unset(PROJECT_COPY_LIBRARIES)
unset(PROJECT_COPY_EXECUTABLES)

# cfssl
project_link_or_copy_files(${PROJECT_THIRD_PARTY_CFSSL_PREBUILT_FILES} "${PROJECT_INSTALL_TOOLS_DIR}/cfssl")

# otelcol
file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/otelcol/bin")
project_link_or_copy_files(${PROJECT_THIRD_PARTY_OTELCOL_BIN_FILES} "${PROJECT_INSTALL_BAS_DIR}/otelcol/bin")
project_link_or_copy_files("${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/otelcol.version"
                           "${PROJECT_INSTALL_BAS_DIR}/otelcol")

# post build script
function(project_setup_runtime_post_build_bash TARGET_NAME SCRIPTS_VAR_NAME)
  if(${SCRIPTS_VAR_NAME})
    unset(POST_BUILD_SCRIPTS)
    foreach(POST_BUILD_SCRIPT ${${SCRIPTS_VAR_NAME}})
      list(
        APPEND
        POST_BUILD_SCRIPTS
        COMMAND
        "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}"
        "${POST_BUILD_SCRIPT}"
        "$<TARGET_FILE:${TARGET_NAME}>"
        "${PROJECT_INSTALL_BAS_DIR}")
    endforeach()
    add_custom_command(
      TARGET ${TARGET_NAME}
      POST_BUILD
      COMMAND ${POST_BUILD_SCRIPTS}
      COMMENT "Run(bash) post build of $<TARGET_FILE:${TARGET_NAME}>: ${${SCRIPTS_VAR_NAME}}")
  endif()
endfunction()

function(project_setup_runtime_post_build_pwsh TARGET_NAME SCRIPTS_VAR_NAME)
  if(ATFRAMEWORK_CMAKE_TOOLSET_PWSH AND ${SCRIPTS_VAR_NAME})
    unset(POST_BUILD_SCRIPTS)
    foreach(POST_BUILD_SCRIPT ${${SCRIPTS_VAR_NAME}})
      list(
        APPEND
        POST_BUILD_SCRIPTS
        COMMAND
        "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}"
        "-File"
        "${POST_BUILD_SCRIPT}"
        "$<TARGET_FILE:${TARGET_FULL_NAME}>"
        "${PROJECT_INSTALL_BAS_DIR}")
    endforeach()
    add_custom_command(
      TARGET ${TARGET_FULL_NAME}
      POST_BUILD
      COMMAND ${POST_BUILD_SCRIPTS}
      COMMENT "Run(pwsh) post build of $<TARGET_FILE:${TARGET_NAME}>: ${${SCRIPTS_VAR_NAME}}")
  endif()
endfunction()
