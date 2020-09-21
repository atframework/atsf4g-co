if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty libcopp ==================
set (3RD_PARTY_LIBCOPP_REPO_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libcopp-repo")

if(LIBCOPP_ROOT)
    set (3RD_PARTY_LIBCOPP_ROOT_DIR ${LIBCOPP_ROOT})
else()
    set (3RD_PARTY_LIBCOPP_ROOT_DIR ${3RD_PARTY_LIBCOPP_REPO_DIR})
endif()

find_package(Libcopp QUIET)
if (Libcopp_FOUND)
    set (3RD_PARTY_LIBCOPP_INC_DIR ${Libcopp_INCLUDE_DIRS})
    set (3RD_PARTY_LIBCOPP_LINK_NAME ${Libcopp_LIBRARIES} ${Libcotask_LIBRARIES})

    EchoWithColor(COLOR GREEN "-- Dependency: libcopp prebuilt found.(inc=${Libcopp_INCLUDE_DIRS})")
else()
    project_git_clone_3rd_party(
        URL "https://github.com/owt5008137/libcopp.git"
        REPO_DIRECTORY ${3RD_PARTY_LIBCOPP_REPO_DIR}
        DEPTH 200
        BRANCH v2
        WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
        CHECK_PATH "CMakeLists.txt"
    )

    if(EXISTS "${3RD_PARTY_LIBCOPP_REPO_DIR}/CMakeLists.txt")
        set (3RD_PARTY_LIBCOPP_INC_DIR "${3RD_PARTY_LIBCOPP_REPO_DIR}/include")
        set (3RD_PARTY_LIBCOPP_LINK_NAME copp cotask)
        set (LIBCOPP_USE_DYNAMIC_LIBRARY ${ATFRAMEWORK_USE_DYNAMIC_LIBRARY} CACHE BOOL "Build dynamic libraries of libcopp" FORCE)
        add_subdirectory(${3RD_PARTY_LIBCOPP_REPO_DIR})

        EchoWithColor(COLOR GREEN "-- Dependency: libcopp found.(repository=${3RD_PARTY_LIBCOPP_REPO_DIR})")
    endif()
endif()

if (TARGET libcopp::copp OR TARGET libcopp::cotask)
    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES libcopp::copp libcopp::cotask)
elseif (TARGET copp OR TARGET cotask)
    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES copp cotask)
else ()
    list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${3RD_PARTY_LIBCOPP_INC_DIR})
    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${Libcopp_LIBRARIES} ${Libcotask_LIBRARIES})
endif ()
