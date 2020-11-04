if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

macro(PROJECT_3RD_PARTY_LUA_IMPORT)
    if(BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
        if (TARGET lua::liblua-dynamic)
            EchoWithColor(COLOR GREEN "-- Dependency: Lua found.(Target: lua::liblua-dynamic)")
            list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES lua::liblua-dynamic)
            if (NOT TARGET lua)
                add_library(lua ALIAS lua::liblua-dynamic)
            endif ()
        endif ()
    else ()
        if (TARGET lua::liblua-static)
            EchoWithColor(COLOR GREEN "-- Dependency: Lua found.(Target: lua::liblua-static)")
            list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES lua::liblua-static)
            if (NOT TARGET lua)
                add_library(lua ALIAS lua::liblua-static)
            endif ()
        endif ()
    endif()
endmacro()

if (NOT TARGET lua::liblua-static AND NOT TARGET lua::liblua-dynamic)
    set (PROJECT_3RD_PARTY_LUA_VERSION "v5.4.1")
    set (PROJECT_3RD_PARTY_LUA_REPO_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/lua-${PROJECT_3RD_PARTY_LUA_VERSION}")
    set (PROJECT_3RD_PARTY_LUA_REPO_URL "https://github.com/lua/lua.git")
    set (PROJECT_3RD_PARTY_LUA_BUILD_DIR "${CMAKE_BINARY_DIR}/deps/lua-${PROJECT_3RD_PARTY_LUA_VERSION}")

    if (NOT EXISTS ${PROJECT_3RD_PARTY_LUA_BUILD_DIR})
        file(MAKE_DIRECTORY ${PROJECT_3RD_PARTY_LUA_BUILD_DIR})
    endif ()
    project_git_clone_3rd_party(
        URL ${PROJECT_3RD_PARTY_LUA_REPO_URL}
        REPO_DIRECTORY ${PROJECT_3RD_PARTY_LUA_REPO_DIR}
        DEPTH 200
        BRANCH ${PROJECT_3RD_PARTY_LUA_VERSION}
        WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
        CHECK_PATH "luaconf.h"
    )
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/build-script" ${PROJECT_3RD_PARTY_LUA_BUILD_DIR})
    PROJECT_3RD_PARTY_LUA_IMPORT()
else()
    PROJECT_3RD_PARTY_LUA_IMPORT()
endif ()
