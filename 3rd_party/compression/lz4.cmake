if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
  include_guard(GLOBAL)
endif()

# =========== 3rdparty lz4 ==================
# force to use prebuilt when using mingw
macro(PROJECT_3RD_PARTY_LZ4_IMPORT)
  if(TARGET lz4::lz4_static)
    echowithcolor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): lz4 found target: lz4::lz4_static")
    # list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES lz4::lz4_static)
  elseif(TARGET lz4::lz4_shared)
    echowithcolor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): lz4 found target: lz4::lz4_shared")
    # list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES lz4::lz4_shared)
  endif()

  if(TARGET lz4::lz4cli)
    project_build_tools_get_imported_location(3RD_PARTY_LZ4_BIN lz4::lz4cli)
    echowithcolor(COLOR GREEN
                  "-- Dependency(${PROJECT_NAME}): lz4 found exec: ${3RD_PARTY_LZ4_BIN}")
  endif()
  if(TARGET lz4::lz4c)
    project_build_tools_get_imported_location(3RD_PARTY_LZ4C_BIN lz4::lz4c)
    echowithcolor(COLOR GREEN
                  "-- Dependency(${PROJECT_NAME}): lz4 found exec: ${3RD_PARTY_LZ4C_BIN}")
  endif()

  if(TARGET lz4::lz4cli OR TARGET lz4::lz4c)
    list(APPEND 3RD_PARTY_COPY_EXECUTABLE_PATTERN "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/lz4*")
  endif()
endmacro()

if(NOT TARGET lz4::lz4_static
   AND NOT TARGET lz4::lz4_shared
   AND NOT TARGET lz4::lz4cli
   AND NOT TARGET lz4::lz4c)
  if(VCPKG_TOOLCHAIN)
    find_package(lz4 QUIET)
    project_3rd_party_lz4_import()
  endif()

  if(NOT TARGET lz4::lz4_static
     AND NOT TARGET lz4::lz4_shared
     AND NOT TARGET lz4::lz4cli
     AND NOT TARGET lz4::lz4c)
    set(PROJECT_3RD_PARTY_LZ4_DEFAULT_VERSION "v1.9.3")

    findconfigurepackage(
      PACKAGE
      lz4
      BUILD_WITH_CMAKE
      CMAKE_INHIRT_BUILD_ENV
      CMAKE_INHIRT_BUILD_ENV_DISABLE_CXX_FLAGS
      CMAKE_FLAGS
      "-DCMAKE_POSITION_INDEPENDENT_CODE=YES"
      "-DBUILD_SHARED_LIBS=OFF"
      "-DBUILD_STATIC_LIBS=ON"
      "-DLZ4_TOP_SOURCE_DIR=${PROJECT_3RD_PARTY_PACKAGE_DIR}/lz4-${PROJECT_3RD_PARTY_LZ4_DEFAULT_VERSION}"
      "-DLZ4_BUILD_CLI=ON"
      "-DLZ4_BUILD_LEGACY_LZ4C=ON"
      "-DLZ4_POSITION_INDEPENDENT_LIB=ON"
      "-DCMAKE_DEBUG_POSTFIX=d"
      WORKING_DIRECTORY
      "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
      PREFIX_DIRECTORY
      "${PROJECT_3RD_PARTY_INSTALL_DIR}"
      SRC_DIRECTORY_NAME
      "lz4-${PROJECT_3RD_PARTY_LZ4_DEFAULT_VERSION}"
      PROJECT_DIRECTORY
      "${CMAKE_CURRENT_LIST_DIR}/lz4-build-script"
      BUILD_DIRECTORY
      "${CMAKE_CURRENT_BINARY_DIR}/deps/lz4-${PROJECT_3RD_PARTY_LZ4_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
      GIT_BRANCH
      "${PROJECT_3RD_PARTY_LZ4_DEFAULT_VERSION}"
      GIT_URL
      "https://github.com/lz4/lz4.git")

    if(NOT TARGET lz4::lz4_static
       AND NOT TARGET lz4::lz4_shared
       AND NOT TARGET lz4::lz4cli
       AND NOT TARGET lz4::lz4c)
      echowithcolor(COLOR YELLOW "-- Dependency: lz4 not found")
    endif()
    project_3rd_party_lz4_import()
  endif()
else()
  project_3rd_party_lz4_import()
endif()
