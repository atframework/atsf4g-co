
# Depend atframe_utils when BUILD_TESTING, need to be imported after atframe_utils
if(NOT TARGET hiredis-happ)
  project_third_party_port_declare(hiredis_happ VERSION "main" GIT_URL "https://github.com/owent/hiredis-happ.git")

  project_git_clone_repository(
    URL
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_HAPP_GIT_URL}"
    REPO_DIRECTORY
    "${PROJECT_THIRD_PARTY_PACKAGE_DIR}/${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_HAPP_SRC_DIRECTORY_NAME}"
    TAG
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_HAPP_VERSION}"
    CHECK_PATH
    "CMakeLists.txt")

  add_subdirectory(
    "${PROJECT_THIRD_PARTY_PACKAGE_DIR}/${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_HAPP_SRC_DIRECTORY_NAME}"
    "${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_HAPP_BUILD_DIR}")

  if(NOT TARGET hiredis-happ)
    echowithcolor(COLOR RED "-- Dependency: hiredis-happ not found")
    message(FATAL_ERROR "hiredis-happ not found")
  endif()
  set(ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_HAPP_LINK_NAME hiredis-happ)
endif()
