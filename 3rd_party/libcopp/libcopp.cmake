if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

macro(PROJECT_3RD_PARTY_LIBCOPP_IMPORT)
    if (TARGET libcopp::cotask)
        EchoWithColor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): libcopp using target: libcopp::cotask")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES libcopp::cotask)
    elseif (TARGET cotask)
        EchoWithColor(COLOR GREEN "-- Dependency(${PROJECT_NAME}): libcopp using target: cotask")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES cotask)
    endif()
endmacro()

if (NOT TARGET libcopp::cotask AND NOT cotask)
    if (VCPKG_TOOLCHAIN)
        find_package(libcopp QUIET)
        PROJECT_3RD_PARTY_LIBCOPP_IMPORT()
    endif ()

    if (NOT TARGET libcopp::cotask AND NOT cotask)
        set (3RD_PARTY_LIBCOPP_DEFAULT_VERSION "v2")
        set (3RD_PARTY_LIBCOPP_REPO_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libcopp-${3RD_PARTY_LIBCOPP_DEFAULT_VERSION}")

        project_git_clone_3rd_party(
            URL "https://github.com/owt5008137/libcopp.git"
            REPO_DIRECTORY ${3RD_PARTY_LIBCOPP_REPO_DIR}
            DEPTH 200
            BRANCH ${3RD_PARTY_LIBCOPP_DEFAULT_VERSION}
            WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
            CHECK_PATH "CMakeLists.txt"
        )

        set (LIBCOPP_USE_DYNAMIC_LIBRARY ${ATFRAMEWORK_USE_DYNAMIC_LIBRARY} CACHE BOOL "Build dynamic libraries of libcopp" FORCE)
        add_subdirectory(${3RD_PARTY_LIBCOPP_REPO_DIR} "${CMAKE_CURRENT_BINARY_DIR}/deps/libcopp-${3RD_PARTY_LIBCOPP_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}")

        PROJECT_3RD_PARTY_LIBCOPP_IMPORT()
    endif ()
else ()
    PROJECT_3RD_PARTY_LIBCOPP_IMPORT()
endif ()