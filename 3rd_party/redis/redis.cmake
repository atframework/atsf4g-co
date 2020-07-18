if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()
# =========== 3rd_party redis ==================
set (3RD_PARTY_REDIS_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set (3RD_PARTY_REDIS_PKG_DIR "${3RD_PARTY_REDIS_BASE_DIR}/pkg")
set (3RD_PARTY_REDIS_HAPP_DIR "${3RD_PARTY_REDIS_BASE_DIR}/hiredis-happ")

set (3RD_PARTY_REDIS_HIREDIS_VERSION "v0.14.1")

if (NOT EXISTS ${3RD_PARTY_REDIS_HAPP_DIR})
    find_package(Git)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init -f --remote "3rd_party/redis/hiredis-happ"
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endif()

if (NOT EXISTS ${3RD_PARTY_REDIS_PKG_DIR})
    file(MAKE_DIRECTORY ${3RD_PARTY_REDIS_PKG_DIR})
endif ()

set(3RD_PARTY_REDIS_HIREDIS_DIR "${3RD_PARTY_REDIS_PKG_DIR}/hiredis-${3RD_PARTY_REDIS_HIREDIS_VERSION}")

project_git_clone_3rd_party(
    URL "https://github.com/redis/hiredis"
    REPO_DIRECTORY ${3RD_PARTY_REDIS_HIREDIS_DIR}
    DEPTH 200
    TAG ${3RD_PARTY_REDIS_HIREDIS_VERSION}
    WORKING_DIRECTORY ${3RD_PARTY_REDIS_PKG_DIR}
)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/build-script")

if (NOT TARGET hiredis)
    EchoWithColor(COLOR RED "-- Dependency: hiredis is required")
    message(FATAL_ERROR "hiredis not found")
endif ()

set(HOREDIS_HAPP_LIBHIREDIS_USING_SRC ON)
add_subdirectory(${3RD_PARTY_REDIS_HAPP_DIR})

if (NOT TARGET hiredis-happ)
    EchoWithColor(COLOR RED "-- Dependency: hiredis-happ not found")
    message(FATAL_ERROR "hiredis-happ not found")
endif ()

set (3RD_PARTY_REDIS_LINK_NAME hiredis-happ hiredis)

list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_REDIS_LINK_NAME})
