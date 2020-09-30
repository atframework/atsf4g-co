if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty zstd ==================
# force to use prebuilt when using mingw
macro(PROJECT_3RD_PARTY_ZSTD_IMPORT)
    if (TARGET zstd::libzstd_shared)
        set(3RD_PARTY_ZSTD_LINK_NAME zstd::libzstd_shared)
        EchoWithColor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): zstd found target: zstd::libzstd_shared")
        # list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES zstd::libzstd_shared)
    elseif (TARGET zstd::libzstd_static)
        set(3RD_PARTY_ZSTD_LINK_NAME zstd::libzstd_static)
        EchoWithColor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): zstd found target: zstd::libzstd_static")
        # list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES zstd::libzstd_static)
    elseif (TARGET zstd::libzstd)
        set(3RD_PARTY_ZSTD_LINK_NAME zstd::libzstd)
        EchoWithColor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): zstd found target: zstd::libzstd")
        # list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES zstd::libzstd)
    else()
        unset(3RD_PARTY_ZSTD_LINK_NAME)
    endif()
    if (TARGET zstd::zstd)
        project_build_tools_get_imported_location(3RD_PARTY_ZSTD_BIN zstd::zstd)
        EchoWithColor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): zstd found exec: ${3RD_PARTY_ZSTD_BIN}")
    elseif (3RD_PARTY_ZSTD_LINK_NAME)
        # Maybe zstd executable not exported, find it by library target
        project_build_tools_get_imported_location(3RD_PARTY_ZSTD_LIB_PATH ${3RD_PARTY_ZSTD_LINK_NAME})
        get_filename_component(3RD_PARTY_ZSTD_LIB_DIR ${3RD_PARTY_ZSTD_LIB_PATH} DIRECTORY)
        get_filename_component(3RD_PARTY_ZSTD_ROOT_DIR ${3RD_PARTY_ZSTD_LIB_DIR} DIRECTORY)
        find_program(3RD_PARTY_ZSTD_BIN NAMES zstd NO_DEFAULT_PATH PATHS ${3RD_PARTY_ZSTD_ROOT_DIR} PATH_SUFFIXES "." "bin")
        if (3RD_PARTY_ZSTD_BIN)
            EchoWithColor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): zstd found exec: ${3RD_PARTY_ZSTD_BIN}")
        endif ()
    endif()

    if (3RD_PARTY_ZSTD_LINK_NAME)
        list(APPEND 3RD_PARTY_COPY_EXECUTABLE_PATTERN "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/zstd*")
    endif()
endmacro()

if (NOT TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd_static AND NOT TARGET zstd::libzstd AND NOT TARGET zstd::zstd)
    if (VCPKG_TOOLCHAIN)
        find_package(zstd QUIET)
        PROJECT_3RD_PARTY_ZSTD_IMPORT()
    endif ()

    if (NOT TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd_static AND NOT TARGET zstd::libzstd AND NOT TARGET zstd::zstd)
        set (3RD_PARTY_ZSTD_DEFAULT_VERSION "v1.4.5")

        FindConfigurePackage(
            PACKAGE zstd
            BUILD_WITH_CMAKE CMAKE_INHIRT_BUILD_ENV CMAKE_INHIRT_BUILD_ENV_DISABLE_CXX_FLAGS
            CMAKE_FLAGS "-DCMAKE_POSITION_INDEPENDENT_CODE=YES" "-DBUILD_SHARED_LIBS=OFF" "-DZSTD_BUILD_TESTS=OFF" "-DZSTD_PROGRAMS_LINK_SHARED=OFF"
                        "-DZSTD_BUILD_STATIC=ON" "-DZSTD_BUILD_SHARED=OFF" "-DZSTD_BUILD_CONTRIB=0" "-DCMAKE_DEBUG_POSTFIX=d"
                        "-DZSTD_BUILD_PROGRAMS=ON" "-DZSTD_MULTITHREAD_SUPPORT=ON" "-DZSTD_ZLIB_SUPPORT=ON" "-DZSTD_LZ4_SUPPORT=ON"
                        "-DLZ4_ROOT_DIR=${PROJECT_3RD_PARTY_INSTALL_DIR}" "-DZLIB_ROOT=${PROJECT_3RD_PARTY_INSTALL_DIR}"
            WORKING_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
            PREFIX_DIRECTORY "${PROJECT_3RD_PARTY_INSTALL_DIR}"
            SRC_DIRECTORY_NAME "zstd-${3RD_PARTY_ZSTD_DEFAULT_VERSION}"
            PROJECT_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}/zstd-${3RD_PARTY_ZSTD_DEFAULT_VERSION}/build/cmake"
            BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/deps/zstd-${3RD_PARTY_ZSTD_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
            GIT_BRANCH "${3RD_PARTY_ZSTD_DEFAULT_VERSION}"
            GIT_URL "https://github.com/facebook/zstd.git"
        )

        if (NOT TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd_static AND NOT TARGET zstd::libzstd AND NOT TARGET zstd::zstd)
            EchoWithColor(COLOR YELLOW "-- Dependency: zstd not found")
        endif()
        PROJECT_3RD_PARTY_ZSTD_IMPORT()
    endif()
else()
    PROJECT_3RD_PARTY_ZSTD_IMPORT()
endif ()