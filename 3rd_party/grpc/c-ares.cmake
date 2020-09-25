# This is c-ares, an asynchronous resolver library.
# https://github.com/c-ares/c-ares.git
# git@github.com:c-ares/c-ares.git
# https://c-ares.haxx.se/

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty c-ares ==================
if (NOT TARGET c-ares::cares AND NOT TARGET c-ares::cares_static AND NOT TARGET c-ares::cares_shared AND NOT CARES_FOUND)
    macro(PROJECT_3RD_PARTY_CARES_IMPORT)
        if (TARGET c-ares::cares)
            message(STATUS "c-ares using target: c-ares::cares")
            set (3RD_PARTY_CARES_LINK_NAME c-ares::cares)
        elseif (TARGET c-ares::cares_static)
            message(STATUS "c-ares using target: c-ares::cares_static")
            set (3RD_PARTY_CARES_LINK_NAME c-ares::cares_static)
        elseif (TARGET c-ares::cares_shared)
            message(STATUS "c-ares using target: c-ares::cares_shared")
            set (3RD_PARTY_CARES_LINK_NAME c-ares::cares_shared)
        elseif(CARES_FOUND AND CARES_LIBRARIES)
            message(STATUS "c-ares support enabled")
            set(3RD_PARTY_CARES_LINK_NAME ${CARES_LIBRARIES})
        else()
            message(STATUS "c-ares support disabled")
        endif()
    endmacro()

    if (VCPKG_TOOLCHAIN)
        find_package(c-ares QUIET)
        PROJECT_3RD_PARTY_CARES_IMPORT()
    endif ()

    set (3RD_PARTY_CARES_DEFAULT_VERSION "cares-1_16_1")

    set(3RD_PARTY_CARES_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
    list(APPEND CMAKE_FIND_ROOT_PATH ${PROJECT_3RD_PARTY_INSTALL_DIR})
    set(c-ares_ROOT ${PROJECT_3RD_PARTY_INSTALL_DIR})
    FindConfigurePackage(
        PACKAGE c-ares
        BUILD_WITH_CMAKE CMAKE_INHIRT_BUILD_ENV CMAKE_INHIRT_BUILD_ENV_DISABLE_CXX_FLAGS
        CMAKE_FLAGS "-DCMAKE_POSITION_INDEPENDENT_CODE=YES" "-DCARES_STATIC_PIC=ON" # "-DBUILD_SHARED_LIBS=OFF" "-DCARES_STATIC=ON" "-DCARES_SHARED=OFF"
        MSVC_CONFIGURE ${gRPC_MSVC_CONFIGURE}
        WORKING_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
        BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/deps/${3RD_PARTY_CARES_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        PREFIX_DIRECTORY "${PROJECT_3RD_PARTY_INSTALL_DIR}"
        SRC_DIRECTORY_NAME "${3RD_PARTY_CARES_DEFAULT_VERSION}"
        GIT_BRANCH "${3RD_PARTY_CARES_DEFAULT_VERSION}"
        GIT_URL "https://github.com/c-ares/c-ares.git"
    )

    if (TARGET c-ares::cares OR TARGET c-ares::cares_static OR TARGET c-ares::cares_shared OR CARES_FOUND)
        PROJECT_3RD_PARTY_CARES_IMPORT()
    endif()
endif ()

if (3RD_PARTY_CARES_BACKUP_FIND_ROOT)
    set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_CARES_BACKUP_FIND_ROOT})
    unset(3RD_PARTY_CARES_BACKUP_FIND_ROOT)
endif ()

