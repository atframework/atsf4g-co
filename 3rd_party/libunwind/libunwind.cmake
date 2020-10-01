if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

macro(PROJECT_3RD_PARTY_LIBUNWIND_IMPORT)
    if (TARGET Libunwind::libunwind)
        message(STATUS "Libunwind found and using target: Libunwind::libunwind")
        set (3RD_PARTY_LIBUNWIND_LINK_NAME Libunwind::libunwind)
        # list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_LIBUNWIND_LINK_NAME})
    elseif(Libunwind_FOUND)
        message(STATUS "Libunwind found and using ${Libunwind_INCLUDE_DIRS}:${Libunwind_LIBRARIES}")
        set(3RD_PARTY_LIBUNWIND_INC_DIR ${Libunwind_INCLUDE_DIRS})
        set(3RD_PARTY_LIBUNWIND_LINK_NAME ${Libunwind_LIBRARIES})

        # if (3RD_PARTY_LIBUNWIND_INC_DIR)
        #     list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${3RD_PARTY_LIBUNWIND_INC_DIR})
        # endif ()
        # list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_LIBUNWIND_LINK_NAME})
    else()
        message(STATUS "libunwind support disabled")
    endif()
endmacro()

# =========== 3rdparty libunwind ==================
if (NOT TARGET Libunwind::libunwind AND NOT Libunwind_FOUND)
    set (3RD_PARTY_LIBUNWIND_DEFAULT_VERSION "v1.5-stable")

    if (PROJECT_GIT_REMOTE_ORIGIN_USE_SSH AND NOT PROJECT_GIT_CLONE_REMOTE_ORIGIN_DISABLE_SSH)
        set (3RD_PARTY_LIBUNWIND_REPO_URL "git@github.com:libunwind/libunwind.git")
    else ()
        set (3RD_PARTY_LIBUNWIND_REPO_URL "https://github.com/libunwind/libunwind.git")
    endif ()

    FindConfigurePackage(
        PACKAGE Libunwind
        BUILD_WITH_CONFIGURE
        PREBUILD_COMMAND "../autogen.sh"
        CONFIGURE_FLAGS "--enable-shared=no" "--enable-static=yes" "--enable-coredump" "--enable-ptrace" "--enable-debug-frame" "--enable-block-signals" "--with-pic=yes"
        WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
        BUILD_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libunwind-${3RD_PARTY_LIBUNWIND_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        PREFIX_DIRECTORY ${PROJECT_3RD_PARTY_INSTALL_DIR}
        SRC_DIRECTORY_NAME "libunwind-${3RD_PARTY_LIBUNWIND_DEFAULT_VERSION}"
        GIT_BRANCH ${3RD_PARTY_LIBUNWIND_DEFAULT_VERSION}
        GIT_URL ${3RD_PARTY_LIBUNWIND_REPO_URL}
    )

    if (NOT Libunwind_FOUND)
        EchoWithColor(COLOR YELLOW "-- Dependency: Libunwind not found and skip import it.")
    else ()
        PROJECT_3RD_PARTY_LIBUNWIND_IMPORT()
    endif()
else()
    PROJECT_3RD_PARTY_LIBUNWIND_IMPORT()
endif ()
