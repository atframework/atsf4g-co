if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty yaml-cpp ==================

macro(PROJECT_3RD_PARTY_YAML_CPP_IMPORT)
    if (TARGET yaml-cpp)
        message(STATUS "yaml-cpp using target: yaml-cpp")
        set (3RD_PARTY_YAML_CPP_LINK_NAME yaml-cpp)
        list(APPEND PROJECT_LIBATAPP_PUBLIC_LINK_NAMES ${3RD_PARTY_YAML_CPP_LINK_NAME})
    endif()
endmacro()

if (VCPKG_TOOLCHAIN)
    find_package(yaml-cpp)
    PROJECT_3RD_PARTY_YAML_CPP_IMPORT()
endif ()

# =========== 3rdparty yaml-cpp ==================
if (NOT TARGET yaml-cpp)
    set (3RD_PARTY_YAML_CPP_DEFAULT_VERSION "0.6.3")

    if (PROJECT_GIT_REMOTE_ORIGIN_USE_SSH AND NOT PROJECT_GIT_CLONE_REMOTE_ORIGIN_DISABLE_SSH)
        set (3RD_PARTY_YAML_CPP_REPO_URL "git@github.com:jbeder/yaml-cpp.git")
    else ()
        set (3RD_PARTY_YAML_CPP_REPO_URL "https://github.com/jbeder/yaml-cpp.git")
    endif ()
    set(3RD_PARTY_YAML_CPP_REPO_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/yaml-cpp-${3RD_PARTY_YAML_CPP_DEFAULT_VERSION}")
    if(NOT EXISTS "${3RD_PARTY_YAML_CPP_REPO_DIR}/CMakeLists.txt")
        if(EXISTS ${3RD_PARTY_YAML_CPP_REPO_DIR})
            file(REMOVE_RECURSE ${3RD_PARTY_YAML_CPP_REPO_DIR})
        endif()
    endif()

    project_git_clone_3rd_party(
        URL ${3RD_PARTY_YAML_CPP_REPO_URL}
        REPO_DIRECTORY ${3RD_PARTY_YAML_CPP_REPO_DIR}
        DEPTH 200
        TAG "yaml-cpp-${3RD_PARTY_YAML_CPP_DEFAULT_VERSION}"
        WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
    )

    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Enable testing for yaml-cpp")
    add_subdirectory(${3RD_PARTY_YAML_CPP_REPO_DIR} "${CMAKE_BINARY_DIR}/deps/yaml-cpp-${3RD_PARTY_YAML_CPP_DEFAULT_VERSION}")

    if (NOT TARGET yaml-cpp)
        EchoWithColor(COLOR RED "-- Dependency: yaml-cpp is required, we can not find prebuilt for yaml-cpp and can not build it from git repository")
        message(FATAL_ERROR "yaml-cpp not found")
    endif()

    PROJECT_3RD_PARTY_YAML_CPP_IMPORT()
endif ()

if (3RD_PARTY_YAML_CPP_BACKUP_FIND_ROOT)
    set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_YAML_CPP_BACKUP_FIND_ROOT})
    unset(3RD_PARTY_YAML_CPP_BACKUP_FIND_ROOT)
endif ()

