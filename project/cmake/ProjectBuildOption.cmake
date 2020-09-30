# 默认配置选项
#####################################################################

# 测试配置选项
set(GTEST_ROOT "" CACHE STRING "GTest root directory")
set(BOOST_ROOT "" CACHE STRING "Boost root directory")
option(PROJECT_TEST_ENABLE_BOOST_UNIT_TEST "Enable boost unit test." OFF)

# 编译的组件
option(PROJECT_ENABLE_SAMPLE "Enable build sample." OFF)
option(PROJECT_ENABLE_UNITTEST "Enable build unit test." OFF)
if(UNIX AND NOT CYGWIN)
    option(LIBUNWIND_ENABLED "Enable libunwind." ON)
endif()

# project name
set(PROJECT_BUILD_NAME "publish" CACHE STRING "Project name")
set(PROJECT_RPC_DB_BUFFER_LENGTH 262144 CACHE STRING "DB package buffer length, used in DB TLS buffer")

# just like ATBUS_MACRO_DATA_SMALL_SIZE
set(ATFRAME_GATEWAY_MACRO_DATA_SMALL_SIZE 3072 CACHE STRING "small message buffer for atgateway connection(used to reduce memory copy when there are many small messages)")

option(PROJECT_RESET_DENPEND_REPOSITORIES "Reset depended repositories if it's already exists." OFF)
option(PROJECT_GIT_CLONE_REMOTE_ORIGIN_DISABLE_SSH "Do not try to use ssh url when clone dependency." OFF)
option(PROJECT_FIND_CONFIGURE_PACKAGE_PARALLEL_BUILD "Parallel building for FindConfigurePackage. It's usually useful for some CI with low memory." ON)

find_package(Git)
if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} config remote.origin.url
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_GIT_REPO_URL
    )
    mark_as_advanced(FORCE PROJECT_GIT_REPO_URL)
    if(PROJECT_GIT_REPO_URL MATCHES "^(http:)|(https:)")
        option(PROJECT_GIT_REMOTE_ORIGIN_USE_SSH "Using ssh git url" OFF)
    else ()
        option(PROJECT_GIT_REMOTE_ORIGIN_USE_SSH "Using ssh git url" ON)
    endif ()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -n 1 --format="%cd %H" --encoding=UTF-8
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_VCS_COMMIT
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -n 1 "--format=%H" --encoding=UTF-8
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_VCS_COMMIT_SHA
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -n 1 --pretty=oneline --encoding=UTF-8
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE SERVER_FRAME_VCS_VERSION
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} branch --show-current
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_VCS_BRANCH
    )

    string(STRIP "${PROJECT_VCS_COMMIT}" PROJECT_VCS_COMMIT)
    string(STRIP "${PROJECT_VCS_BRANCH}" PROJECT_VCS_BRANCH)
endif ()

if (NOT PROJECT_VCS_COMMIT_SHA)
    set(PROJECT_VCS_COMMIT_SHA "UNKNOWN_COMMIT_SHA")
else ()
    string(STRIP "${PROJECT_VCS_COMMIT_SHA}" PROJECT_VCS_COMMIT_SHA)
endif ()

if (NOT PROJECT_VCS_COMMIT_SHA)
    set(PROJECT_VCS_COMMIT_SHA "UNKNOWN_COMMIT_SHA")
    set(PROJECT_VCS_COMMIT_SHORT_SHA "UNKNOWN_COMMIT")
else ()
    string(STRIP "${PROJECT_VCS_COMMIT_SHA}" PROJECT_VCS_COMMIT_SHA)
    if (GIT_FOUND)
        foreach(SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH "8" "12" "16" "24" "32")
            string(SUBSTRING ${PROJECT_VCS_COMMIT_SHA} 0 ${SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH} PROJECT_VCS_COMMIT_SHORT_SHA)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} show "--format=%H" --encoding=UTF-8 ${PROJECT_VCS_COMMIT_SHORT_SHA}
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                RESULT_VARIABLE SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT
                OUTPUT_QUIET ERROR_QUIET
            )
            if (${SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT} EQUAL 0)
                break()
            endif ()
        endforeach()
        if (NOT ${SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT} EQUAL 0)
            set(PROJECT_VCS_COMMIT_SHORT_SHA ${PROJECT_VCS_COMMIT_SHA})
        endif()
        unset(SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH)
        unset(SERVER_FRAME_VCS_COMMIT_SHORT_SHA_TEST_LENGTH_RESULT)
    else ()
        set(PROJECT_VCS_COMMIT_SHORT_SHA ${PROJECT_VCS_COMMIT_SHA})
    endif ()
endif ()

option(ATFRAMEWORK_USE_DYNAMIC_LIBRARY "Build and linking with dynamic libraries." OFF)
