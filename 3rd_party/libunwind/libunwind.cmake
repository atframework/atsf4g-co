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
    if (NOT 3RD_PARTY_LIBUNWIND_BASE_DIR)
        set (3RD_PARTY_LIBUNWIND_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    endif()

    set (3RD_PARTY_LIBUNWIND_PKG_DIR "${3RD_PARTY_LIBUNWIND_BASE_DIR}/pkg")

    set (3RD_PARTY_LIBUNWIND_DEFAULT_VERSION "v1.5-stable")
    set (3RD_PARTY_LIBUNWIND_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/prebuilt/${PROJECT_PREBUILT_PLATFORM_NAME}")

    if(NOT EXISTS ${3RD_PARTY_LIBUNWIND_PKG_DIR})
        file(MAKE_DIRECTORY ${3RD_PARTY_LIBUNWIND_PKG_DIR})
    endif()

    if (PROJECT_GIT_REMOTE_ORIGIN_USE_SSH AND NOT PROJECT_GIT_CLONE_REMOTE_ORIGIN_DISABLE_SSH)
        set (3RD_PARTY_LIBUNWIND_REPO_URL "git@github.com:libunwind/libunwind.git")
    else ()
        set (3RD_PARTY_LIBUNWIND_REPO_URL "https://github.com/libunwind/libunwind.git")
    endif ()
    set(3RD_PARTY_LIBUNWIND_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
    list(APPEND CMAKE_FIND_ROOT_PATH ${3RD_PARTY_LIBUNWIND_ROOT_DIR})
    FindConfigurePackage(
        PACKAGE Libunwind
        BUILD_WITH_CONFIGURE
        PREBUILD_COMMAND "../autogen.sh"
        CONFIGURE_FLAGS "--enable-shared=no" "--enable-static=yes" "--enable-coredump" "--enable-ptrace" "--enable-debug-frame" "--enable-block-signals" "--with-pic=yes"
        WORKING_DIRECTORY ${3RD_PARTY_LIBUNWIND_PKG_DIR}
        BUILD_DIRECTORY "${3RD_PARTY_LIBUNWIND_PKG_DIR}/libunwind-${3RD_PARTY_LIBUNWIND_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        PREFIX_DIRECTORY ${3RD_PARTY_LIBUNWIND_ROOT_DIR}
        SRC_DIRECTORY_NAME "libunwind-${3RD_PARTY_LIBUNWIND_DEFAULT_VERSION}"
        GIT_BRANCH ${3RD_PARTY_LIBUNWIND_DEFAULT_VERSION}
        GIT_URL ${3RD_PARTY_LIBUNWIND_REPO_URL}
    )

    if (NOT Libunwind_FOUND)
        EchoWithColor(COLOR YELLOW "-- Dependency: Libunwind not found and skip import it.")
    else ()
        PROJECT_3RD_PARTY_LIBUNWIND_IMPORT()
    endif()
endif ()

if (3RD_PARTY_LIBUNWIND_BACKUP_FIND_ROOT)
    set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_LIBUNWIND_BACKUP_FIND_ROOT})
    unset(3RD_PARTY_LIBUNWIND_BACKUP_FIND_ROOT)
endif ()

