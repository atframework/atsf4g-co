
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


if (NOT EXISTS "${3RD_PARTY_REDIS_PKG_DIR}/hiredis-${3RD_PARTY_REDIS_HIREDIS_VERSION}/.git")
    message(STATUS "hiredis not found try to pull it.")
    find_package(Git)
    if(GIT_FOUND)
        message(STATUS "git found: ${GIT_EXECUTABLE}")
        execute_process(COMMAND ${GIT_EXECUTABLE} clone --depth=100 -b ${3RD_PARTY_REDIS_HIREDIS_VERSION} "https://github.com/redis/hiredis" hiredis-${3RD_PARTY_REDIS_HIREDIS_VERSION}
            WORKING_DIRECTORY ${3RD_PARTY_REDIS_PKG_DIR}
        )
    endif()
endif()

set(DISABLE_TESTS ON)
set(ENABLE_EXAMPLES OFF)
add_subdirectory("${3RD_PARTY_REDIS_PKG_DIR}/hiredis-${3RD_PARTY_REDIS_HIREDIS_VERSION}")
unset(DISABLE_TESTS)
unset(ENABLE_EXAMPLES)

if (NOT TARGET hiredis)
    EchoWithColor(COLOR RED "-- Dependency: hiredis is required")
    message(FATAL_ERROR "hiredis not found")
endif ()

add_subdirectory(${3RD_PARTY_REDIS_HAPP_DIR})

set (3RD_PARTY_REDIS_LINK_NAME hiredis-happ hiredis)

