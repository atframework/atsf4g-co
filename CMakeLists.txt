cmake_minimum_required(VERSION 3.12.0)
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0022 NEW)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.13")
    cmake_policy(SET CMP0074 NEW)
endif()

project(atf4g-co)

include("${CMAKE_SOURCE_DIR}/3rd_party/3rd_party.prepare.cmake")
include("${CMAKE_SOURCE_DIR}/atframework/atframework.prepare.cmake")

########################################################################
# CMake 模块 (递归包含模块, 带颜色输出模块, 平台检测模块)
set (PROJECT_CMAKE_MODULE_DIR "${ATFRAME_UTILS_ROOT}/project/cmake")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_CMAKE_MODULE_DIR}/modules")


include("${PROJECT_CMAKE_MODULE_DIR}/modules/IncludeDirectoryRecurse.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/modules/EchoWithColor.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/modules/FindConfigurePackage.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/modules/ProjectBuildTools.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/project/cmake/ProjectBuildOption.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/FindPlatform.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/ProjectTools.cmake")

#####################################################################
# 导入编译器和编译选项配置
include("${PROJECT_CMAKE_MODULE_DIR}/CompilerOption.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/TargetOption.cmake")
EchoWithColor(COLOR GREEN "-- Build Type: ${CMAKE_BUILD_TYPE}")

# 导入工程工具
include("${CMAKE_CURRENT_LIST_DIR}/project/cmake/ProjectTools.cmake")

# installer directory
if (NOT PROJECT_INSTALL_BAS_DIR)
    set (PROJECT_INSTALL_BAS_DIR "${PROJECT_BINARY_DIR}/${PROJECT_BUILD_NAME}")
endif ()
if(NOT EXISTS ${PROJECT_INSTALL_BAS_DIR})
    file(MAKE_DIRECTORY ${PROJECT_INSTALL_BAS_DIR})
    message(STATUS "create ${PROJECT_INSTALL_BAS_DIR} for build target.")
endif()

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_LIBDIR}")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/${CMAKE_INSTALL_BINDIR}")

set (PROJECT_INSTALL_RES_DIR "${PROJECT_INSTALL_BAS_DIR}/resource")
set (PROJECT_INSTALL_TOOLS_DIR  "${PROJECT_INSTALL_BAS_DIR}/tools")
set (PROJECT_INSTALL_SHARED_DIR "${PROJECT_INSTALL_TOOLS_DIR}/shared")
set (PROJECT_INSTALL_RES_PBD_DIR "${PROJECT_INSTALL_RES_DIR}/pbdesc")
file(MAKE_DIRECTORY "${PROJECT_INSTALL_TOOLS_DIR}/bin")

file(MAKE_DIRECTORY ${PROJECT_INSTALL_SHARED_DIR})
file(MAKE_DIRECTORY ${PROJECT_INSTALL_RES_PBD_DIR})

########################################################################
if(MINGW)
    if("" STREQUAL "$ENV{MINGW_MOUNT_POINT}")
        if(NOT "" STREQUAL "$ENV{MINGW_PREFIX}")
            set(ENV{MINGW_MOUNT_POINT} $ENV{MINGW_PREFIX})
        else()
            message(FATAL_ERROR "You must set environment MINGW_PREFIX or MINGW_MOUNT_POINT when using mingw")
        endif()
    endif()

    list(APPEND CMAKE_SYSTEM_LIBRARY_PATH "$ENV{MINGW_MOUNT_POINT}/lib" "$ENV{MINGW_MOUNT_POINT}/libexec")
    list(APPEND CMAKE_SYSTEM_INCLUDE_PATH "$ENV{MINGW_MOUNT_POINT}/include")
    list(APPEND CMAKE_SYSTEM_PROGRAM_PATH "$ENV{MINGW_MOUNT_POINT}/bin" "$ENV{MINGW_MOUNT_POINT}/libexec")
    message(STATUS "MinGW => CMAKE_LIBRARY_PATH=${CMAKE_LIBRARY_PATH}")
    message(STATUS "MinGW => CMAKE_SYSTEM_LIBRARY_PATH=${CMAKE_SYSTEM_LIBRARY_PATH}")
    message(STATUS "MinGW => CMAKE_SYSTEM_INCLUDE_PATH=${CMAKE_SYSTEM_INCLUDE_PATH}")
    message(STATUS "MinGW => CMAKE_INCLUDE_PATH=${CMAKE_INCLUDE_PATH}")
    message(STATUS "MinGW => CMAKE_SYSTEM_FRAMEWORK_PATH=${CMAKE_SYSTEM_FRAMEWORK_PATH}")
    message(STATUS "MinGW => CMAKE_PROGRAM_PATH=${CMAKE_PROGRAM_PATH}")
    message(STATUS "MinGW => CMAKE_SYSTEM_PROGRAM_PATH=${CMAKE_SYSTEM_PROGRAM_PATH}")
endif()

# 导入项目配置
## 导入所有 macro 定义
include("${PROJECT_3RD_PARTY_ROOT_DIR}/3rd_party.macro.cmake")
# atframwork 内有依赖关系，所以不能直接导，要手动导入
include("${ATFRAMEWORK_BASE_DIR}/atframework.macro.cmake")

# 增加相对链接路径
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
if(CMAKE_SIZEOF_VOID_P MATCHES 8)
    set(CMAKE_BUILD_RPATH "../../lib64" "../../../lib64" "../../lib" "../../../lib")
else ()
    set(CMAKE_BUILD_RPATH "../../lib" "../../../lib")
endif ()

## 导入所有工程项目
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/atframework")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/src")

if (PROJECT_ENABLE_SAMPLE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/sample")
endif()
#
# if (PROJECT_ENABLE_UNITTEST)
#     include ("${ATFRAME_UTILS_ROOT}/test/test.utils-macro.cmake")
#     add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/test")
# endif()
#

# copy 资源
file(GLOB PROJECT_INSTALL_DST_LIST  "${CMAKE_CURRENT_LIST_DIR}/install/*")
foreach(INSTALL_DST_ITEM ${PROJECT_INSTALL_DST_LIST})
    EchoWithColor(COLOR GREEN "-- Install: Copy ${INSTALL_DST_ITEM} ...")
    file(
        COPY "${INSTALL_DST_ITEM}"
        DESTINATION "${PROJECT_INSTALL_BAS_DIR}"
        USE_SOURCE_PERMISSIONS
    )
endforeach()

# add +rx permission
if(CYGWIN OR UNIX OR APPLE)
    execute_process (
    	COMMAND chmod +rx -R "${PROJECT_INSTALL_BAS_DIR}"
    	WORKING_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}"
    )
endif()

# 安装目录
install(
    DIRECTORY "${PROJECT_INSTALL_BAS_DIR}"
    DESTINATION "."
    USE_SOURCE_PERMISSIONS
    PATTERN ".svn" EXCLUDE
)

if (PROJECT_INSTALL_EXPORT_TARGETS)
    export(EXPORT ${PROJECT_INSTALL_EXPORT_NAME} NAMESPACE "${PROJECT_NAME}::" FILE ${PROJECT_INSTALL_EXPORT_FILE})
    install(EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
        NAMESPACE "${PROJECT_NAME}::"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake"
    )
endif()
