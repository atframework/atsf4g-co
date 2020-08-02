# 默认配置选项
#####################################################################

# 测试配置选项
set(GTEST_ROOT "" CACHE STRING "GTest root directory")
set(BOOST_ROOT "" CACHE STRING "Boost root directory")
option(PROJECT_TEST_ENABLE_BOOST_UNIT_TEST "Enable boost unit test." OFF)

# 编译的组件
option(PROJECT_ENABLE_SAMPLE "Enable build sample." OFF)
option(PROJECT_ENABLE_UNITTEST "Enable build unit test." OFF)

# project name
set(PROJECT_BUILD_NAME "publish" CACHE STRING "Project name")
set(PROJECT_RPC_DB_BUFFER_LENGTH 262144 CACHE STRING "DB package buffer length, used in DB TLS buffer")

# just like ATBUS_MACRO_DATA_SMALL_SIZE
set(ATFRAME_GATEWAY_MACRO_DATA_SMALL_SIZE 3072 CACHE STRING "small message buffer for atgateway connection(used to reduce memory copy when there are many small messages)")

option(PROJECT_RESET_DENPEND_REPOSITORIES "Reset depended repositories if it's already exists." OFF)
option(PROJECT_GIT_CLONE_REMOTE_ORIGIN_DISABLE_SSH "Do not try to use ssh url when clone dependency." OFF)
option(PROJECT_FIND_CONFIGURE_PACKAGE_PARALLEL_BUILD "Parallel building for FindConfigurePackage. It's usually useful for some CI with low memory." ON)

if (NOT PROJECT_GIT_REMOTE_ORIGIN_USE_SSH)
    if (EXISTS "${PROJECT_SOURCE_DIR}/.git")
        find_package(Git)
        if(NOT EXISTS ${ATFRAMEWORK_ATFRAME_UTILS_REPO_DIR})
            execute_process(COMMAND ${GIT_EXECUTABLE} config "remote.origin.url"
                OUTPUT_VARIABLE PROJECT_GIT_REMOTE_ORIGIN_URL
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            string(REGEX MATCH "^http(s?):" PROJECT_GIT_REMOTE_ORIGIN_USE_SSH "${PROJECT_GIT_REMOTE_ORIGIN_URL}")
        endif()
    endif()
endif()