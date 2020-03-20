
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty jemalloc ==================
set (3RD_PARTY_JEMALLOC_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set (3RD_PARTY_JEMALLOC_PKG_DIR "${CMAKE_CURRENT_LIST_DIR}/pkg")
set (3RD_PARTY_JEMALLOC_PKG_VERSION 5.2.1)
set (3RD_PARTY_JEMALLOC_MODE "release")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set (3RD_PARTY_JEMALLOC_MODE "debug")
endif()

if(JEMALLOC_ROOT)
    set (3RD_PARTY_JEMALLOC_ROOT_DIR "${JEMALLOC_ROOT}")
else()
    set (3RD_PARTY_JEMALLOC_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/prebuilt/${PROJECT_PREBUILT_PLATFORM_NAME}/${3RD_PARTY_JEMALLOC_MODE}")
endif()

if (NOT MSVC)
    file(MAKE_DIRECTORY ${3RD_PARTY_JEMALLOC_PKG_DIR})

    set (3RD_PARTY_JEMALLOC_BUILD_OPTIONS "--enable-debug")
    if ("release" STREQUAL ${3RD_PARTY_JEMALLOC_MODE})
        set (3RD_PARTY_JEMALLOC_BUILD_OPTIONS "")
    endif()

    FindConfigurePackage(
        PACKAGE Jemalloc
        BUILD_WITH_CONFIGURE
        CONFIGURE_FLAGS "--enable-static=no --enable-prof --enable-valgrind --enable-lazy-lock --enable-xmalloc --enable-mremap --enable-utrace --enable-munmap ${3RD_PARTY_JEMALLOC_BUILD_OPTIONS}"
        MAKE_FLAGS "-j4"
        # PREBUILD_COMMAND "./autogen.sh"
        WORKING_DIRECTORY "${3RD_PARTY_JEMALLOC_PKG_DIR}"
        BUILD_DIRECTORY "${3RD_PARTY_JEMALLOC_PKG_DIR}/jemalloc-${3RD_PARTY_JEMALLOC_PKG_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        PREFIX_DIRECTORY "${3RD_PARTY_JEMALLOC_ROOT_DIR}"
        TAR_URL "https://github.com/jemalloc/jemalloc/releases/download/${3RD_PARTY_JEMALLOC_PKG_VERSION}/jemalloc-${3RD_PARTY_JEMALLOC_PKG_VERSION}.tar.bz2"
    )

    if(JEMALLOC_FOUND)
        EchoWithColor(COLOR GREEN "-- Dependency: Jemalloc found.(${Jemalloc_LIBRARY_DIRS})")

        set (3RD_PARTY_JEMALLOC_INC_DIR ${Jemalloc_INCLUDE_DIRS})
        set (3RD_PARTY_JEMALLOC_LIB_DIR ${Jemalloc_LIBRARY_DIRS})

        include_directories(${3RD_PARTY_JEMALLOC_INC_DIR})

        file(GLOB 3RD_PARTY_JEMALLOC_ALL_LIB_FILES  
            "${3RD_PARTY_JEMALLOC_LIB_DIR}/libjemalloc*.so*"
            "${3RD_PARTY_JEMALLOC_LIB_DIR}/libjemalloc*.dll*"
        )
        project_copy_shared_lib(${3RD_PARTY_JEMALLOC_ALL_LIB_FILES} ${PROJECT_INSTALL_SHARED_DIR})
    endif()
endif()
