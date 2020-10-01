
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty jemalloc ==================
if (NOT MSVC AND NOT MINGW AND (NOT 3RD_PARTY_JEMALLOC_INC_DIR OR NOT 3RD_PARTY_JEMALLOC_LIB_DIR))
    set (3RD_PARTY_JEMALLOC_PKG_VERSION 5.2.1)
    set (3RD_PARTY_JEMALLOC_MODE "release")

    # if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    #     set (3RD_PARTY_JEMALLOC_MODE "debug")
    # endif()

    set (3RD_PARTY_JEMALLOC_BUILD_OPTIONS "--enable-debug")
    if ("release" STREQUAL ${3RD_PARTY_JEMALLOC_MODE})
        set (3RD_PARTY_JEMALLOC_BUILD_OPTIONS "")
    endif()

    FindConfigurePackage(
        PACKAGE Jemalloc
        BUILD_WITH_CONFIGURE
        CONFIGURE_FLAGS "--enable-static=no" "--enable-prof" "--enable-valgrind" "--enable-lazy-lock" "--enable-xmalloc" "--enable-mremap" "--enable-utrace" 
                        "--enable-munmap" ${3RD_PARTY_JEMALLOC_BUILD_OPTIONS}
        INSTALL_TARGET "install_bin" "install_include" "install_lib"
        MAKE_FLAGS "-j4"
        PREBUILD_COMMAND "./autogen.sh"
        WORKING_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
        BUILD_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}/jemalloc-${3RD_PARTY_JEMALLOC_PKG_VERSION}"
        PREFIX_DIRECTORY ${PROJECT_3RD_PARTY_INSTALL_DIR}
        SRC_DIRECTORY_NAME "jemalloc-${3RD_PARTY_JEMALLOC_PKG_VERSION}"
        GIT_BRANCH ${3RD_PARTY_JEMALLOC_PKG_VERSION}
        GIT_URL "https://github.com/jemalloc/jemalloc.git"
    )

    if(JEMALLOC_FOUND)
        EchoWithColor(COLOR GREEN "-- Dependency: Jemalloc found.(${Jemalloc_LIBRARY_DIRS})")

        set (3RD_PARTY_JEMALLOC_INC_DIR ${Jemalloc_INCLUDE_DIRS})
        set (3RD_PARTY_JEMALLOC_LIB_DIR ${Jemalloc_LIBRARY_DIRS})

        list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${3RD_PARTY_JEMALLOC_INC_DIR})
    endif()
endif()
