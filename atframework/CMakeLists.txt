# 基本框架程序
add_subdirectory(${ATFRAME_UTILS_ROOT})
add_subdirectory(${LIBATBUS_ROOT})
add_subdirectory(${LIBATAPP_ROOT})

# 基础服务
add_subdirectory("${ATFRAMEWORK_BASE_DIR}/service")

# 导出库
add_subdirectory("${ATFRAMEWORK_BASE_DIR}/export")