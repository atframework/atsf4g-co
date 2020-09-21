if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

if (NOT 3RD_PARTY_RAPIDJSON_INC_DIR)
    # =========== 3rdparty rapidjson ==================
    set (3RD_PARTY_RAPIDJSON_VERSION master)
    set (3RD_PARTY_RAPIDJSON_REPO_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/rapidjson-${3RD_PARTY_RAPIDJSON_VERSION}")

    if (Rapidjson_ROOT)
        set(RAPIDJSON_ROOT ${Rapidjson_ROOT})
    endif()

    find_package(Rapidjson)
    if(NOT Rapidjson_FOUND)
        project_git_clone_3rd_party(
            URL "https://github.com/Tencent/rapidjson.git"
            REPO_DIRECTORY ${3RD_PARTY_RAPIDJSON_REPO_DIR}
            DEPTH 200
            BRANCH ${3RD_PARTY_RAPIDJSON_VERSION}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        set(Rapidjson_ROOT ${3RD_PARTY_RAPIDJSON_REPO_DIR})
        set(RAPIDJSON_ROOT ${3RD_PARTY_RAPIDJSON_REPO_DIR})
        set (3RD_PARTY_RAPIDJSON_ROOT_DIR ${3RD_PARTY_RAPIDJSON_REPO_DIR})
        find_package(Rapidjson)
    else()
        set(3RD_PARTY_RAPIDJSON_ROOT_DIR ${RAPIDJSON_ROOT})
    endif()

    if(Rapidjson_FOUND)
        EchoWithColor(COLOR GREEN "-- Dependency: rapidjson found.(${Rapidjson_INCLUDE_DIRS})")
    else()
        EchoWithColor(COLOR RED "-- Dependency: rapidjson is required")
        message(FATAL_ERROR "rapidjson not found")
    endif()


    set (3RD_PARTY_RAPIDJSON_INC_DIR ${Rapidjson_INCLUDE_DIRS})
    list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${3RD_PARTY_RAPIDJSON_INC_DIR})
endif()