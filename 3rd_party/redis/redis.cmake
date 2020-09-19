if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()
# =========== 3rd_party redis ==================

macro(PROJECT_3RD_PARTY_REDIS_HIREDIS_IMPORT)
    if (TARGET hiredis::hiredis_ssl_static)
        message(STATUS "hiredis using target: hiredis::hiredis_ssl_static")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES hiredis::hiredis_ssl_static)
    elseif (TARGET hiredis::hiredis_static)
        message(STATUS "hiredis using target: hiredis::hiredis_static")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES hiredis::hiredis_static)
    elseif (TARGET hiredis::hiredis_ssl)
        message(STATUS "hiredis using target: hiredis::hiredis_ssl")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES hiredis::hiredis_ssl)
    elseif (TARGET hiredis::hiredis)
        message(STATUS "hiredis using target: hiredis::hiredis")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES hiredis::hiredis)
    else()
        message(STATUS "hiredis support disabled")
    endif()
endmacro()

# if (VCPKG_TOOLCHAIN)
#     find_package(hiredis)
#     PROJECT_3RD_PARTY_REDIS_HIREDIS_IMPORT()
# endif ()

if (NOT TARGET hiredis::hiredis_ssl_static AND NOT TARGET hiredis::hiredis_static AND NOT TARGET hiredis::hiredis_ssl AND NOT TARGET hiredis::hiredis)
    set (3RD_PARTY_REDIS_HIREDIS_VERSION "v1.0.0")
    set (3RD_PARTY_REDIS_HIREDIS_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/hiredis-${3RD_PARTY_REDIS_HIREDIS_VERSION}")

    set(3RD_PARTY_REDIS_HIREDIS_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
    list(APPEND CMAKE_FIND_ROOT_PATH ${PROJECT_3RD_PARTY_INSTALL_DIR})

    set(3RD_PARTY_REDIS_HIREDIS_OPTIONS "-DDISABLE_TESTS=YES" "-DENABLE_EXAMPLES=OFF")
    if (OPENSSL_FOUND)
        list(APPEND 3RD_PARTY_REDIS_HIREDIS_OPTIONS "-DENABLE_SSL=ON")
    endif ()
    FindConfigurePackage(
        PACKAGE hiredis
        BUILD_WITH_CMAKE CMAKE_INHIRT_BUILD_ENV CMAKE_INHIRT_BUILD_ENV_DISABLE_CXX_FLAGS
        CMAKE_FLAGS ${3RD_PARTY_REDIS_HIREDIS_OPTIONS}
        WORKING_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
        BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/deps/hiredis-${3RD_PARTY_REDIS_HIREDIS_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        PREFIX_DIRECTORY "${PROJECT_3RD_PARTY_INSTALL_DIR}"
        SRC_DIRECTORY_NAME "hiredis-${3RD_PARTY_REDIS_HIREDIS_VERSION}"
        GIT_BRANCH "${3RD_PARTY_REDIS_HIREDIS_VERSION}"
        GIT_URL "https://github.com/redis/hiredis.git"
    )

    if (NOT hiredis_FOUND)
        EchoWithColor(COLOR RED "-- Dependency: hiredis is required, we can not find prebuilt for hiredis and can not build from the sources")
        message(FATAL_ERROR "hiredis not found")
    endif()

    PROJECT_3RD_PARTY_REDIS_HIREDIS_IMPORT()
endif ()

if (NOT hiredis_FOUND)
    EchoWithColor(COLOR RED "-- Dependency: hiredis is required")
    message(FATAL_ERROR "hiredis not found")
endif ()

if (NOT TARGET hiredis-happ)
    set (3RD_PARTY_REDIS_HAPP_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/hiredis-happ-repo")

    project_git_clone_3rd_party(
        URL "https://github.com/owt5008137/hiredis-happ.git"
        REPO_DIRECTORY ${3RD_PARTY_REDIS_HAPP_DIR}
        DEPTH 200
        BRANCH master
        WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
        CHECK_PATH "CMakeLists.txt"
    )

    set(HOREDIS_HAPP_LIBHIREDIS_USING_SRC ON)
    add_subdirectory(${3RD_PARTY_REDIS_HAPP_DIR} "${CMAKE_BINARY_DIR}/deps/hiredis-happ-repo")

    if (NOT TARGET hiredis-happ)
        EchoWithColor(COLOR RED "-- Dependency: hiredis-happ not found")
        message(FATAL_ERROR "hiredis-happ not found")
    endif ()

    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES hiredis-happ)
endif ()

if (3RD_PARTY_REDIS_HIREDIS_BACKUP_FIND_ROOT)
    set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_REDIS_HIREDIS_BACKUP_FIND_ROOT})
    unset(3RD_PARTY_REDIS_HIREDIS_BACKUP_FIND_ROOT)
endif ()
