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