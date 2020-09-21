if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

macro(PROJECT_3RD_PARTY_LUA_IMPORT)
    if (TARGET lua)
        EchoWithColor(COLOR GREEN "-- Dependency: Lua found.(Target: lua)")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES lua)
    elseif (LUA_FOUND)
        EchoWithColor(COLOR GREEN "-- Dependency: Lua found.(${LUA_INCLUDE_DIR}:${LUA_LIBRARIES})")
        if (LUA_INCLUDE_DIR)
            list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
            set(3RD_PARTY_LUA_INC_DIR ${LUA_INCLUDE_DIR})
        endif ()

        if (LUA_LIBRARIES)
            list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${LUA_LIBRARIES})
            set(3RD_PARTY_LUA_LINK_NAME ${LUA_LIBRARIES})
        endif ()
    endif ()
endmacro()

if (NOT TARGET lua AND NOT (3RD_PARTY_LUA_INC_DIR AND 3RD_PARTY_LUA_LINK_NAME))
    if (VCPKG_TOOLCHAIN)
        find_package(Lua QUIET)
        PROJECT_3RD_PARTY_LUA_IMPORT()
    endif ()
endif()

if (NOT TARGET lua AND NOT (3RD_PARTY_LUA_INC_DIR AND 3RD_PARTY_LUA_LINK_NAME))
    set(LUA_VERSION_MAJOR "5" CACHE STRING "the major version of Lua" FORCE)
    set(LUA_VERSION_MINOR "4" CACHE STRING "the minor version of Lua" FORCE)
    set(LUA_VERSION_PATCH "0" CACHE STRING "the patch version of Lua" FORCE)
    set (3RD_PARTY_LUA_VERSION "v${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}.${LUA_VERSION_PATCH}")
    set (3RD_PARTY_LUA_REPO_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/lua-${3RD_PARTY_LUA_VERSION}")

    find_path(3RD_PARTY_LUA_INC_DIR NAMES "lua.h" PATH_SUFFIXES include src PATHS ${PROJECT_3RD_PARTY_INSTALL_DIR} NO_DEFAULT_PATH)
    find_library(3RD_PARTY_LUA_LINK_NAME NAMES lua liblua PATH_SUFFIXES lib lib64 PATHS ${PROJECT_3RD_PARTY_INSTALL_DIR} NO_DEFAULT_PATH)
    if (NOT 3RD_PARTY_LUA_INC_DIR OR NOT 3RD_PARTY_LUA_LINK_NAME)
        if (PROJECT_GIT_REMOTE_ORIGIN_USE_SSH AND NOT PROJECT_GIT_CLONE_REMOTE_ORIGIN_DISABLE_SSH)
            set (3RD_PARTY_LUA_REPO_URL "git@github.com:lua/lua.git")
        else ()
            set (3RD_PARTY_LUA_REPO_URL "https://github.com/lua/lua.git")
        endif ()
        project_git_clone_3rd_party(
            URL ${3RD_PARTY_LUA_REPO_URL}
            REPO_DIRECTORY ${3RD_PARTY_LUA_REPO_DIR}
            DEPTH 200
            TAG ${3RD_PARTY_LUA_VERSION}
            WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
        )
        set (3RD_PARTY_LUA_BUILD_DIR "${CMAKE_BINARY_DIR}/deps/lua-${3RD_PARTY_LUA_VERSION}")
        find_path(3RD_PARTY_LUA_SRC_DIR NAMES "lua.h" "lauxlib.h" PATH_SUFFIXES src PATHS ${3RD_PARTY_LUA_REPO_DIR} NO_DEFAULT_PATH)
        if (3RD_PARTY_LUA_SRC_DIR)
            set(3RD_PARTY_LUA_INC_DIR ${3RD_PARTY_LUA_SRC_DIR})
            set(3RD_PARTY_LUA_LINK_NAME lua)

            if (NOT EXISTS ${3RD_PARTY_LUA_BUILD_DIR})
                file(MAKE_DIRECTORY ${3RD_PARTY_LUA_BUILD_DIR})
            endif ()

            # patch for ios SDK 11.0 or upper
            if (CMAKE_HOST_APPLE OR APPLE)
                execute_process(COMMAND 
                    sed -E -i.bak "s/system[^;]+;/luaL_error(L, \"can not call this function on macOS or ios\");/g" loslib.c 
                    WORKING_DIRECTORY ${3RD_PARTY_LUA_SRC_DIR}
                )
            endif ()
            add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/build-script" ${3RD_PARTY_LUA_BUILD_DIR})
        endif ()
    endif ()

    if (3RD_PARTY_LUA_INC_DIR AND 3RD_PARTY_LUA_LINK_NAME)
        # support for find_package(Lua)
        set(LUA_FOUND YES CACHE BOOL "if false, do not try to link to Lua" FORCE)
        set(LUA_LIBRARIES ${3RD_PARTY_LUA_LINK_NAME} CACHE STRING "both lua and lualib" FORCE)
        set(LUA_INCLUDE_DIR ${3RD_PARTY_LUA_INC_DIR} CACHE PATH "where to find lua.h" FORCE)
        PROJECT_3RD_PARTY_LUA_IMPORT()
    else ()
        EchoWithColor(COLOR YELLOW "-- Dependency: Lua not found.")
        unset(LUA_FOUND CACHE)
        unset(LUA_LIBRARIES CACHE)
        unset(LUA_INCLUDE_DIR CACHE)
        unset(LUA_VERSION_MAJOR CACHE)
        unset(LUA_VERSION_MINOR CACHE)
        unset(LUA_VERSION_PATCH CACHE)
    endif ()
endif ()


unset(3RD_PARTY_LUA_INC_DIR CACHE)
unset(3RD_PARTY_LUA_LINK_NAME CACHE)
