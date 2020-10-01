# 基本框架程序
if(TARGET ${3RD_PARTY_ATFRAME_UTILS_LINK_NAME})
    message(STATUS "${3RD_PARTY_ATFRAME_UTILS_LINK_NAME} already exist, use it for ${PROJECT_NAME} directly.")
else()
    add_subdirectory(${ATFRAME_UTILS_ROOT})
endif()

add_subdirectory(${LIBATBUS_ROOT})
add_subdirectory(${LIBATAPP_ROOT})

# 基础服务
add_subdirectory("${ATFRAMEWORK_BASE_DIR}/service")

# 导出库
add_subdirectory("${ATFRAMEWORK_BASE_DIR}/export")