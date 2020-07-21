set (PROJECT_3RD_PARTY_PYTHON_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/.modules")
set (PROJECT_3RD_PARTY_PYTHON_PIP_SOURCE "-i" "https://mirrors.tencent.com/pypi/simple/")
if (NOT EXISTS ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR})
    file(MAKE_DIRECTORY ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}) 
endif ()

find_package(Python COMPONENTS Interpreter)
if (NOT Python_Interpreter_FOUND)
    message(FATAL_ERROR "Python is required but not found")
endif()

message(STATUS "Install dependency python modules into ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}")
execute_process(
    COMMAND ${Python_EXECUTABLE} "-m" "pip" "install" ${PROJECT_3RD_PARTY_PYTHON_PIP_SOURCE} "--upgrade" "--prefix" ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR} "pip"
    WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}
)

execute_process(
    COMMAND ${Python_EXECUTABLE} "-m" "pip" "install" ${PROJECT_3RD_PARTY_PYTHON_PIP_SOURCE} "--prefix" ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR} "-r" "${CMAKE_CURRENT_LIST_DIR}/requirements.txt"
    WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PYTHON_MODULE_DIR}
)
