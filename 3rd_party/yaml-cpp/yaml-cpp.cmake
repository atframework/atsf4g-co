if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty yaml-cpp ==================
macro(PROJECT_3RD_PARTY_YAML_CPP_IMPORT)
    if (TARGET yaml-cpp::yaml-cpp)
        message(STATUS "yaml-cpp using target: yaml-cpp::yaml-cpp")
        set (3RD_PARTY_YAML_CPP_LINK_NAME yaml-cpp::yaml-cpp)
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_YAML_CPP_LINK_NAME})
    elseif (TARGET yaml-cpp)
        message(STATUS "yaml-cpp using target: yaml-cpp")
        set (3RD_PARTY_YAML_CPP_LINK_NAME yaml-cpp)
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_YAML_CPP_LINK_NAME})
    elseif (YAML_CPP_INCLUDE_DIR AND YAML_CPP_LIBRARIES)
        message(STATUS "yaml-cpp using: ${YAML_CPP_INCLUDE_DIR}:${YAML_CPP_LIBRARIES}")
        set (3RD_PARTY_YAML_CPP_LINK_NAME ${YAML_CPP_LIBRARIES})
        list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${YAML_CPP_INCLUDE_DIR})
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_YAML_CPP_LINK_NAME})
    endif()
endmacro()

if (VCPKG_TOOLCHAIN)
    find_package(yaml-cpp QUIET)
    PROJECT_3RD_PARTY_YAML_CPP_IMPORT()
endif ()

# =========== 3rdparty yaml-cpp ==================
if (NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp AND (NOT YAML_CPP_INCLUDE_DIR OR NOT YAML_CPP_LIBRARIES))
    set (3RD_PARTY_YAML_CPP_DEFAULT_VERSION "0.6.3")

    if (PROJECT_GIT_REMOTE_ORIGIN_USE_SSH AND NOT PROJECT_GIT_CLONE_REMOTE_ORIGIN_DISABLE_SSH)
        set (3RD_PARTY_YAML_CPP_REPO_URL "git@github.com:jbeder/yaml-cpp.git")
    else ()
        set (3RD_PARTY_YAML_CPP_REPO_URL "https://github.com/jbeder/yaml-cpp.git")
    endif ()

    FindConfigurePackage(
        PACKAGE yaml-cpp
        BUILD_WITH_CMAKE CMAKE_INHIRT_BUILD_ENV
        CMAKE_FLAGS "-DCMAKE_POSITION_INDEPENDENT_CODE=YES" "-DYAML_CPP_BUILD_TESTS=OFF" "-DYAML_CPP_INSTALL=ON" # "-DBUILD_SHARED_LIBS=OFF"
        MSVC_CONFIGURE ${CMAKE_BUILD_TYPE}
        WORKING_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
        BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/deps/yaml-cpp-${3RD_PARTY_YAML_CPP_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        PREFIX_DIRECTORY "${PROJECT_3RD_PARTY_INSTALL_DIR}"
        SRC_DIRECTORY_NAME "yaml-cpp-${3RD_PARTY_YAML_CPP_DEFAULT_VERSION}"
        GIT_BRANCH "yaml-cpp-${3RD_PARTY_YAML_CPP_DEFAULT_VERSION}"
        GIT_URL "${3RD_PARTY_YAML_CPP_REPO_URL}"
    )

    if (NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp AND (NOT YAML_CPP_INCLUDE_DIR OR NOT YAML_CPP_LIBRARIES))
        EchoWithColor(COLOR RED "-- Dependency: yaml-cpp is required, we can not find prebuilt for yaml-cpp and can not build it from git repository")
        message(FATAL_ERROR "yaml-cpp not found")
    endif()

    PROJECT_3RD_PARTY_YAML_CPP_IMPORT()
else()
    PROJECT_3RD_PARTY_YAML_CPP_IMPORT()
endif ()
