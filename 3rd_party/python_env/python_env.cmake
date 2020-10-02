set (PROJECT_3RD_PARTY_PYTHON_MODULE_DIR "${PROJECT_3RD_PARTY_INSTALL_DIR}/.modules")
set (PROJECT_3RD_PARTY_PYTHON_PIP_SOURCE "-i" "https://mirrors.tencent.com/pypi/simple/")
if (NOT EXISTS ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR})
    file(MAKE_DIRECTORY ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}) 
endif ()

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
    cmake_policy(SET CMP0094 NEW)
endif()

find_package(Python3 COMPONENTS Interpreter)

# Patch for python3 binary
if (NOT Python3_Interpreter_FOUND AND UNIX)
    find_program(Python3_EXECUTABLE NAMES python3)
    if (Python3_EXECUTABLE)
        get_filename_component(Python3_BIN_DIR ${Python3_EXECUTABLE} DIRECTORY)
        get_filename_component(Python3_ROOT_DIR ${Python3_BIN_DIR} DIRECTORY CACHE)
        find_package(Python3 COMPONENTS Interpreter)
    endif ()
endif()
if (NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR "Python is required but not found")
endif()

message(STATUS "Install dependency python(${Python3_EXECUTABLE}) modules into ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}")
# execute_process(
#     COMMAND ${Python3_EXECUTABLE} "-m" "pip" "install" ${PROJECT_3RD_PARTY_PYTHON_PIP_SOURCE} "--upgrade" "--prefix" ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR} "pip"
#     WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}
# )

execute_process(
    COMMAND ${Python3_EXECUTABLE} "-m" "pip" "install" ${PROJECT_3RD_PARTY_PYTHON_PIP_SOURCE} "--prefix" ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR} "-r" "${CMAKE_CURRENT_LIST_DIR}/requirements.txt"
    WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}
)
