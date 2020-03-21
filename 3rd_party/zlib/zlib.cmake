if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty zlib ==================
if (NOT 3RD_PARTY_ZLIB_BASE_DIR)
    set (3RD_PARTY_ZLIB_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()

set (3RD_PARTY_ZLIB_PKG_DIR "${3RD_PARTY_ZLIB_BASE_DIR}/pkg")

set (3RD_PARTY_ZLIB_DEFAULT_VERSION "1.2.11")
set (3RD_PARTY_ZLIB_ROOT_DIR "${3RD_PARTY_ZLIB_BASE_DIR}/prebuilt/${PROJECT_PREBUILT_PLATFORM_NAME}")

macro(PROJECT_3RD_PARTY_ZLIB_IMPORT)
    if (ZLIB_FOUND)
        # find static library first
        EchoWithColor(COLOR GREEN "-- Dependency: zlib found.(${ZLIB_LIBRARIES})")

        if (ZLIB_INCLUDE_DIRS)
            list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
        endif ()

        if(ZLIB_LIBRARIES)
            list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${ZLIB_LIBRARIES})
        endif()
    endif()
endmacro()

if (VCPKG_TOOLCHAIN)
    find_package(ZLIB)
    PROJECT_3RD_PARTY_ZLIB_IMPORT()
endif ()

# force to use prebuilt when using mingw
if (NOT ZLIB_FOUND)
    if(NOT EXISTS ${3RD_PARTY_ZLIB_PKG_DIR})
        file(MAKE_DIRECTORY ${3RD_PARTY_ZLIB_PKG_DIR})
    endif()
    set(ZLIB_ROOT ${3RD_PARTY_ZLIB_ROOT_DIR})

    FindConfigurePackage(
        PACKAGE ZLIB
        BUILD_WITH_CMAKE
        CMAKE_FLAGS "-DCMAKE_POSITION_INDEPENDENT_CODE=YES" "-DBUILD_SHARED_LIBS=OFF"
        MAKE_FLAGS "-j8"
        WORKING_DIRECTORY "${3RD_PARTY_ZLIB_PKG_DIR}"
        PREFIX_DIRECTORY "${3RD_PARTY_ZLIB_ROOT_DIR}"
        SRC_DIRECTORY_NAME "zlib-${3RD_PARTY_ZLIB_DEFAULT_VERSION}"
        BUILD_DIRECTORY "${3RD_PARTY_ZLIB_PKG_DIR}/zlib-${3RD_PARTY_ZLIB_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        TAR_URL "http://zlib.net/zlib-${3RD_PARTY_ZLIB_DEFAULT_VERSION}.tar.gz"
    )

    if(NOT ZLIB_FOUND)
        EchoWithColor(COLOR RED "-- Dependency: zlib is required")
        message(FATAL_ERROR "zlib not found")
    endif()
    PROJECT_3RD_PARTY_ZLIB_IMPORT()
endif ()