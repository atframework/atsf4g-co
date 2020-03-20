if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

if (NOT 3RD_PARTY_LUA_INC_DIR OR NOT 3RD_PARTY_LUA_LINK_NAME)
    if (NOT 3RD_PARTY_LUA_BASE_DIR)
        set(3RD_PARTY_LUA_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    endif ()
    set (3RD_PARTY_LUA_REPO_DIR "${3RD_PARTY_LUA_BASE_DIR}/repo")
    set(LUA_VERSION_MAJOR "5" CACHE STRING "the major version of Lua" FORCE)
    set(LUA_VERSION_MINOR "3" CACHE STRING "the minor version of Lua" FORCE)
    set(LUA_VERSION_PATCH "5" CACHE STRING "the patch version of Lua" FORCE)
    set (3RD_PARTY_LUA_VERSION "v${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}.${LUA_VERSION_PATCH}")

    if(LUA_ROOT)
        set (3RD_PARTY_LUA_ROOT_DIR ${LUA_ROOT})
    else()
        set (3RD_PARTY_LUA_ROOT_DIR ${3RD_PARTY_LUA_REPO_DIR})
    endif()

    if (NOT 3RD_PARTY_LUA_INC_DIR OR NOT 3RD_PARTY_LUA_LINK_NAME)
        find_path(3RD_PARTY_LUA_INC_DIR NAMES "lua.h" PATH_SUFFIXES include src PATHS ${3RD_PARTY_LUA_ROOT_DIR} NO_DEFAULT_PATH)
        find_library(3RD_PARTY_LUA_LINK_NAME NAMES lua liblua PATH_SUFFIXES lib lib64 PATHS ${3RD_PARTY_LUA_ROOT_DIR} NO_DEFAULT_PATH)
        if (NOT 3RD_PARTY_LUA_INC_DIR OR NOT 3RD_PARTY_LUA_LINK_NAME)
            message(STATUS "Clone lua ${3RD_PARTY_LUA_VERSION}")
            project_git_clone_3rd_party(
                URL "https://github.com/lua/lua.git"
                REPO_DIRECTORY ${3RD_PARTY_LUA_REPO_DIR}
                DEPTH 200
                TAG ${3RD_PARTY_LUA_VERSION}
                WORKING_DIRECTORY ${3RD_PARTY_LUA_BASE_DIR}
            )
            set (3RD_PARTY_LUA_BUILD_DIR "${CMAKE_BINARY_DIR}/deps/${3RD_PARTY_LUA_LINK_NAME}-${3RD_PARTY_LUA_VERSION}")
            find_path(3RD_PARTY_LUA_SRC_DIR NAMES "lua.h" "lauxlib.h" PATH_SUFFIXES src PATHS ${3RD_PARTY_LUA_ROOT_DIR} NO_DEFAULT_PATH)
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
    endif ()

    if (3RD_PARTY_LUA_INC_DIR AND 3RD_PARTY_LUA_LINK_NAME)
        EchoWithColor(COLOR GREEN "-- Dependency: Lua found.(${3RD_PARTY_LUA_ROOT_DIR})")
        # support for find_package(Lua)
        set(LUA_FOUND YES CACHE BOOL "if false, do not try to link to Lua" FORCE)
        set(LUA_LIBRARIES ${3RD_PARTY_LUA_LINK_NAME} CACHE STRING "both lua and lualib" FORCE)
        set(LUA_INCLUDE_DIR ${3RD_PARTY_LUA_INC_DIR} YES CACHE PATH "where to find lua.h" FORCE)
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

if (TARGET lua)
    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES lua)
elseif (LUA_FOUND)
    if (3RD_PARTY_LUA_INC_DIR)
        list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${3RD_PARTY_LUA_INC_DIR})
    endif ()

    if (3RD_PARTY_LUA_LINK_NAME)
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_LUA_LINK_NAME})
    endif ()
endif ()