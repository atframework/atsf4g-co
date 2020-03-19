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
set(PROJECT_RPC_DB_BUFFER_LENGTH 131072 CACHE STRING "DB package buffer length, used in DB TLS buffer")

# just like ATBUS_MACRO_DATA_SMALL_SIZE
set(ATFRAME_GATEWAY_MACRO_DATA_SMALL_SIZE 3072 CACHE STRING "small message buffer for atgateway connection(used to reduce memory copy when there are many small messages)")

# libatbus
set(ATBUS_MACRO_MSG_LIMIT 65536 CACHE STRING "message size limit of libatbus")

option(PROJECT_RESET_DENPEND_REPOSITORIES "Reset depended repositories if it's already exists." OFF)
