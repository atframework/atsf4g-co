# CMakeLists.txt for src/robot (Go-based robot/testing framework)
# Requires: golang, python, java/jdk, helm, go-task

cmake_minimum_required(VERSION 3.24.0)

# ============================================================================
# 1. Find required tools
# ============================================================================

# --- Go ---
find_program(
  GO_EXECUTABLE
  NAMES go go.exe
  DOC "Go compiler")
if(NOT GO_EXECUTABLE)
  message(FATAL_ERROR "Go compiler not found. Please install Go from https://go.dev/dl/")
endif()
execute_process(
  COMMAND "${GO_EXECUTABLE}" version
  OUTPUT_VARIABLE _GO_VERSION_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _GO_VERSION_RESULT)
if(_GO_VERSION_RESULT EQUAL 0)
  message(STATUS "Found Go: ${GO_EXECUTABLE} (${_GO_VERSION_OUTPUT})")
else()
  message(FATAL_ERROR "Go found at ${GO_EXECUTABLE} but failed to get version")
endif()

# --- Python ---
if(NOT DEFINED Python3_EXECUTABLE)
  find_package(Python3 COMPONENTS Interpreter)

  # Patch for python3 binary
  if(NOT Python3_Interpreter_FOUND AND UNIX)
    find_program(Python3_EXECUTABLE NAMES python3)
  endif()
  if(NOT Python3_EXECUTABLE)
    message(FATAL_ERROR "Python not found. Please install Python 3.x")
  endif()
endif()
execute_process(
  COMMAND "${Python3_EXECUTABLE}" --version
  OUTPUT_VARIABLE _PYTHON_VERSION_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _PYTHON_VERSION_RESULT)
if(_PYTHON_VERSION_RESULT EQUAL 0)
  message(STATUS "Found Python: ${Python3_EXECUTABLE} (${_PYTHON_VERSION_OUTPUT})")
else()
  message(FATAL_ERROR "Python found at ${Python3_EXECUTABLE} but failed to get version")
endif()

# --- Java / JDK ---
find_program(
  JAVA_EXECUTABLE
  NAMES java java.exe
  DOC "Java runtime")
if(NOT JAVA_EXECUTABLE)
  message(FATAL_ERROR "Java not found. Please install a JDK (https://adoptium.net/)")
endif()
execute_process(
  COMMAND "${JAVA_EXECUTABLE}" -version
  ERROR_VARIABLE _JAVA_VERSION_OUTPUT
  ERROR_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _JAVA_VERSION_RESULT)
if(_JAVA_VERSION_RESULT EQUAL 0)
  message(STATUS "Found Java: ${JAVA_EXECUTABLE} (${_JAVA_VERSION_OUTPUT})")
else()
  message(FATAL_ERROR "Java found at ${JAVA_EXECUTABLE} but failed to get version")
endif()

# --- Helm ---
find_program(
  HELM_EXECUTABLE
  NAMES helm helm.exe
  HINTS "${PROJECT_INSTALL_TOOLS_DIR}/helm"
  DOC "Helm package manager")
if(NOT HELM_EXECUTABLE)
  message(FATAL_ERROR "Helm not found in install tools directory.")
endif()
execute_process(
  COMMAND "${HELM_EXECUTABLE}" version --short
  OUTPUT_VARIABLE _HELM_VERSION_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _HELM_VERSION_RESULT)
if(_HELM_VERSION_RESULT EQUAL 0)
  message(STATUS "Found Helm: ${HELM_EXECUTABLE} (${_HELM_VERSION_OUTPUT})")
else()
  message(FATAL_ERROR "Helm found at ${HELM_EXECUTABLE} but failed to get version")
endif()

# --- go-task ---
find_program(
  TASK_EXECUTABLE
  NAMES task task.exe
  HINTS "${PROJECT_INSTALL_TOOLS_DIR}/go-task"
  DOC "go-task runner")
if(NOT TASK_EXECUTABLE)
  message(FATAL_ERROR "go-task not found in install tools directory.")
endif()
execute_process(
  COMMAND "${TASK_EXECUTABLE}" --version
  OUTPUT_VARIABLE _TASK_VERSION_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _TASK_VERSION_RESULT)
if(_TASK_VERSION_RESULT EQUAL 0)
  message(STATUS "Found go-task: ${TASK_EXECUTABLE} (${_TASK_VERSION_OUTPUT})")
else()
  message(FATAL_ERROR "go-task found at ${TASK_EXECUTABLE} but failed to get version")
endif()

# ============================================================================
# 1. Configure paths
# ============================================================================

set(ROBOT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(ROBOT_INSTALL_PATH "${PROJECT_INSTALL_BAS_DIR}/robot")

message(STATUS "Robot source dir: ${ROBOT_SOURCE_DIR}")
message(STATUS "Robot install path: ${ROBOT_INSTALL_PATH}")

maybe_populate_submodule(ROBOT_DEPENDENCY_ATFRAME_UTILS_GO "src/robot/atframework/atframe-utils-go"
                         "${PROJECT_SOURCE_DIR}/src/robot/atframework/atframe-utils-go")

maybe_populate_submodule(ROBOT_DEPENDENCY_ROBOT_GO "src/robot/atframework/robot-go"
                         "${PROJECT_SOURCE_DIR}/src/robot/atframework/robot-go")

# ============================================================================
# 1. Custom target: run task all
# ============================================================================

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "INSTALL_PATH=${ROBOT_INSTALL_PATH}" "${TASK_EXECUTABLE}" all
  WORKING_DIRECTORY "${ROBOT_SOURCE_DIR}"
)