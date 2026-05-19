# CMakeLists.txt for src/robot (Go-based robot/testing framework)
# Requires: golang, python, java/jdk, helm, go-task

cmake_minimum_required(VERSION 3.24.0)

set(ROBOT_AUTO_INSTALL_TOOLS ON CACHE BOOL "Automatically install missing robot build tools into the build directory")
set(ROBOT_GO_VERSION "1.25.1" CACHE STRING "Go version to auto-install when it is missing")
set(ROBOT_HELM_VERSION "3.19.0" CACHE STRING "Helm version to auto-install when it is missing")
set(ROBOT_GO_TASK_VERSION "3.45.4" CACHE STRING "go-task version to auto-install when it is missing")
set(ROBOT_JAVA_MAJOR_VERSION "21" CACHE STRING "Java major version to auto-install when it is missing")

set(ROBOT_LOCAL_TOOL_ROOT "${CMAKE_BINARY_DIR}/_tools")
set(ROBOT_LOCAL_DOWNLOAD_DIR "${ROBOT_LOCAL_TOOL_ROOT}/downloads")
set(ROBOT_LOCAL_TOOL_BIN_DIR "${ROBOT_LOCAL_TOOL_ROOT}/bin")
set(ROBOT_LOCAL_GO_ROOT "${ROBOT_LOCAL_TOOL_ROOT}/go")
set(ROBOT_LOCAL_PYTHON_ROOT "${ROBOT_LOCAL_TOOL_ROOT}/miniconda3")
set(ROBOT_LOCAL_JDK_ROOT "${ROBOT_LOCAL_TOOL_ROOT}/jdk")
set(ROBOT_LOCAL_JAVA_HOME "${ROBOT_LOCAL_JDK_ROOT}/current")
set(ROBOT_LOCAL_HELM_ROOT "${ROBOT_LOCAL_TOOL_ROOT}/helm")

if(CMAKE_HOST_WIN32)
  set(ROBOT_PATH_SEPARATOR ";")
else()
  set(ROBOT_PATH_SEPARATOR ":")
endif()

set(ROBOT_HOST_OS "")
set(ROBOT_GO_ARCH "")
set(ROBOT_HELM_ARCH "")
set(ROBOT_MINICONDA_ARCH "")
set(ROBOT_JAVA_ARCH "")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(ROBOT_HOST_OS "linux")
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(ROBOT_GO_ARCH "amd64")
    set(ROBOT_HELM_ARCH "amd64")
    set(ROBOT_MINICONDA_ARCH "x86_64")
    set(ROBOT_JAVA_ARCH "x64")
  endif()
endif()

if(ROBOT_HOST_OS AND ROBOT_GO_ARCH AND ROBOT_HELM_ARCH AND ROBOT_MINICONDA_ARCH AND ROBOT_JAVA_ARCH)
  set(ROBOT_CAN_AUTO_INSTALL TRUE)
else()
  set(ROBOT_CAN_AUTO_INSTALL FALSE)
endif()

file(MAKE_DIRECTORY "${ROBOT_LOCAL_DOWNLOAD_DIR}" "${ROBOT_LOCAL_TOOL_BIN_DIR}")

function(robot_download_file url output_path label)
  get_filename_component(_download_dir "${output_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${_download_dir}")

  file(DOWNLOAD "${url}" "${output_path}" SHOW_PROGRESS STATUS _download_status LOG _download_log TLS_VERIFY ON)
  list(GET _download_status 0 _download_status_code)
  list(GET _download_status 1 _download_status_text)
  if(NOT _download_status_code EQUAL 0)
    message(
      FATAL_ERROR
        "Failed to download ${label} from ${url}: [${_download_status_code}] ${_download_status_text}\n"
        "${_download_log}")
  endif()
endfunction()

function(robot_ensure_auto_install_supported tool_name help_url)
  if(NOT ROBOT_AUTO_INSTALL_TOOLS)
    message(
      FATAL_ERROR
        "${tool_name} not found and automatic installation is disabled.\n"
        "Please install from ${help_url}")
  endif()

  if(NOT ROBOT_CAN_AUTO_INSTALL)
    message(
      FATAL_ERROR
        "${tool_name} not found. Automatic installation currently supports Linux x86_64 only.\n"
        "Please install from ${help_url}")
  endif()
endfunction()

function(robot_locate_jdk install_root)
  set(_java_executable "")
  set(_javac_executable "")
  set(_java_home "")

  file(GLOB _jdk_candidates LIST_DIRECTORIES true "${install_root}/*")
  foreach(_candidate IN LISTS _jdk_candidates)
    if(EXISTS "${_candidate}/bin/java" AND EXISTS "${_candidate}/bin/javac")
      set(_java_home "${_candidate}")
      set(_java_executable "${_candidate}/bin/java")
      set(_javac_executable "${_candidate}/bin/javac")
      break()
    endif()
  endforeach()

  set(ROBOT_JAVA_HOME "${_java_home}" PARENT_SCOPE)
  set(JAVA_EXECUTABLE "${_java_executable}" PARENT_SCOPE)
  set(JAVAC_EXECUTABLE "${_javac_executable}" PARENT_SCOPE)
endfunction()

function(robot_install_go)
  robot_ensure_auto_install_supported("Go compiler" "https://go.dev/dl/")

  set(_go_archive "${ROBOT_LOCAL_DOWNLOAD_DIR}/go${ROBOT_GO_VERSION}.${ROBOT_HOST_OS}-${ROBOT_GO_ARCH}.tar.gz")
  set(_go_url "https://go.dev/dl/go${ROBOT_GO_VERSION}.${ROBOT_HOST_OS}-${ROBOT_GO_ARCH}.tar.gz")

  message(STATUS "Go compiler not found. Try installing Go ${ROBOT_GO_VERSION} into ${ROBOT_LOCAL_GO_ROOT}")
  robot_download_file("${_go_url}" "${_go_archive}" "Go ${ROBOT_GO_VERSION}")

  file(REMOVE_RECURSE "${ROBOT_LOCAL_GO_ROOT}")
  file(ARCHIVE_EXTRACT INPUT "${_go_archive}" DESTINATION "${ROBOT_LOCAL_TOOL_ROOT}")

  if(NOT EXISTS "${ROBOT_LOCAL_GO_ROOT}/bin/go")
    message(FATAL_ERROR "Go compiler not found and automatic installation failed to produce ${ROBOT_LOCAL_GO_ROOT}/bin/go")
  endif()

  set(GO_EXECUTABLE "${ROBOT_LOCAL_GO_ROOT}/bin/go" PARENT_SCOPE)
endfunction()

function(robot_install_python)
  robot_ensure_auto_install_supported("Python" "https://www.python.org/downloads/")

  find_program(_ROBOT_BASH_EXECUTABLE NAMES bash DOC "Bash shell")
  if(NOT _ROBOT_BASH_EXECUTABLE)
    message(FATAL_ERROR "Python automatic installation requires bash")
  endif()

  set(_python_installer "${ROBOT_LOCAL_DOWNLOAD_DIR}/Miniconda3-latest-Linux-${ROBOT_MINICONDA_ARCH}.sh")
  set(_python_url "https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-${ROBOT_MINICONDA_ARCH}.sh")

  message(STATUS "Python not found. Try installing Miniconda into ${ROBOT_LOCAL_PYTHON_ROOT}")
  robot_download_file("${_python_url}" "${_python_installer}" "Miniconda")

  file(REMOVE_RECURSE "${ROBOT_LOCAL_PYTHON_ROOT}")
  execute_process(
    COMMAND "${_ROBOT_BASH_EXECUTABLE}" "${_python_installer}" -b -p "${ROBOT_LOCAL_PYTHON_ROOT}"
    OUTPUT_VARIABLE _python_install_output
    ERROR_VARIABLE _python_install_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _python_install_result)

  if(NOT _python_install_result EQUAL 0 OR NOT EXISTS "${ROBOT_LOCAL_PYTHON_ROOT}/bin/python3")
    message(
      FATAL_ERROR
        "Python not found and automatic installation failed.\n"
        "Install command: ${_ROBOT_BASH_EXECUTABLE} ${_python_installer} -b -p ${ROBOT_LOCAL_PYTHON_ROOT}\n"
        "Install result: ${_python_install_result}\n"
        "Install stdout:\n${_python_install_output}\n"
        "Install stderr:\n${_python_install_error}\n"
        "Please install Python 3.x manually.")
  endif()

  set(PYTHON_EXECUTABLE "${ROBOT_LOCAL_PYTHON_ROOT}/bin/python3" PARENT_SCOPE)
endfunction()

function(robot_install_jdk)
  robot_ensure_auto_install_supported("Java runtime" "https://adoptium.net/")

  set(_jdk_archive "${ROBOT_LOCAL_DOWNLOAD_DIR}/temurin-jdk-${ROBOT_JAVA_MAJOR_VERSION}-${ROBOT_HOST_OS}-${ROBOT_JAVA_ARCH}.tar.gz")
  set(_jdk_url
      "https://api.adoptium.net/v3/binary/latest/${ROBOT_JAVA_MAJOR_VERSION}/ga/linux/${ROBOT_JAVA_ARCH}/jdk/hotspot/normal/eclipse")

  message(STATUS "Java runtime not found. Try installing Temurin JDK ${ROBOT_JAVA_MAJOR_VERSION} into ${ROBOT_LOCAL_JAVA_HOME}")
  robot_download_file("${_jdk_url}" "${_jdk_archive}" "Temurin JDK ${ROBOT_JAVA_MAJOR_VERSION}")

  file(REMOVE_RECURSE "${ROBOT_LOCAL_JDK_ROOT}")
  file(MAKE_DIRECTORY "${ROBOT_LOCAL_JDK_ROOT}")
  file(ARCHIVE_EXTRACT INPUT "${_jdk_archive}" DESTINATION "${ROBOT_LOCAL_JDK_ROOT}")
  robot_locate_jdk("${ROBOT_LOCAL_JDK_ROOT}")

  if(NOT JAVA_EXECUTABLE OR NOT JAVAC_EXECUTABLE OR NOT ROBOT_JAVA_HOME)
    message(FATAL_ERROR "Java runtime not found and automatic installation failed to produce a usable JDK under ${ROBOT_LOCAL_JDK_ROOT}")
  endif()

  file(REMOVE_RECURSE "${ROBOT_LOCAL_JAVA_HOME}")
  file(RENAME "${ROBOT_JAVA_HOME}" "${ROBOT_LOCAL_JAVA_HOME}")

  set(JAVA_EXECUTABLE "${ROBOT_LOCAL_JAVA_HOME}/bin/java" PARENT_SCOPE)
  set(JAVAC_EXECUTABLE "${ROBOT_LOCAL_JAVA_HOME}/bin/javac" PARENT_SCOPE)
  set(ROBOT_JAVA_HOME "${ROBOT_LOCAL_JAVA_HOME}" PARENT_SCOPE)
endfunction()

function(robot_install_helm)
  robot_ensure_auto_install_supported("Helm" "https://helm.sh/docs/intro/install/")

  set(_helm_archive "${ROBOT_LOCAL_DOWNLOAD_DIR}/helm-v${ROBOT_HELM_VERSION}-${ROBOT_HOST_OS}-${ROBOT_HELM_ARCH}.tar.gz")
  set(_helm_url "https://get.helm.sh/helm-v${ROBOT_HELM_VERSION}-${ROBOT_HOST_OS}-${ROBOT_HELM_ARCH}.tar.gz")
  set(_helm_executable "${ROBOT_LOCAL_HELM_ROOT}/${ROBOT_HOST_OS}-${ROBOT_HELM_ARCH}/helm")

  message(STATUS "Helm not found. Try installing Helm ${ROBOT_HELM_VERSION} into ${ROBOT_LOCAL_HELM_ROOT}")
  robot_download_file("${_helm_url}" "${_helm_archive}" "Helm ${ROBOT_HELM_VERSION}")

  file(REMOVE_RECURSE "${ROBOT_LOCAL_HELM_ROOT}")
  file(MAKE_DIRECTORY "${ROBOT_LOCAL_HELM_ROOT}")
  file(ARCHIVE_EXTRACT INPUT "${_helm_archive}" DESTINATION "${ROBOT_LOCAL_HELM_ROOT}")

  if(NOT EXISTS "${_helm_executable}")
    message(FATAL_ERROR "Helm not found and automatic installation failed to produce ${_helm_executable}")
  endif()

  set(HELM_EXECUTABLE "${_helm_executable}" PARENT_SCOPE)
endfunction()

function(robot_install_task)
  if(NOT GO_EXECUTABLE)
    message(FATAL_ERROR "go-task automatic installation requires a working Go compiler")
  endif()

  set(_task_package "github.com/go-task/task/v3/cmd/task@v${ROBOT_GO_TASK_VERSION}")

  message(STATUS "go-task not found. Try installing ${_task_package} into ${ROBOT_LOCAL_TOOL_BIN_DIR}")
  file(MAKE_DIRECTORY "${ROBOT_LOCAL_TOOL_BIN_DIR}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GOBIN=${ROBOT_LOCAL_TOOL_BIN_DIR}" "${GO_EXECUTABLE}" install
            "${_task_package}"
    OUTPUT_VARIABLE _task_install_output
    ERROR_VARIABLE _task_install_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _task_install_result)

  if(NOT _task_install_result EQUAL 0 OR NOT EXISTS "${ROBOT_LOCAL_TOOL_BIN_DIR}/task")
    message(
      FATAL_ERROR
        "go-task not found and automatic installation failed.\n"
        "Install command: ${GO_EXECUTABLE} install ${_task_package}\n"
        "Install result: ${_task_install_result}\n"
        "Install stdout:\n${_task_install_output}\n"
        "Install stderr:\n${_task_install_error}\n"
        "Please install from https://taskfile.dev/installation/")
  endif()

  set(TASK_EXECUTABLE "${ROBOT_LOCAL_TOOL_BIN_DIR}/task" PARENT_SCOPE)
endfunction()

# ============================================================================
# 1. Find required tools
# ============================================================================

# --- Go ---
find_program(
  GO_EXECUTABLE
  NAMES go go.exe
  HINTS "${ROBOT_LOCAL_GO_ROOT}/bin"
  DOC "Go compiler")
if(NOT GO_EXECUTABLE)
  robot_install_go()
endif()
if(NOT GO_EXECUTABLE)
  message(FATAL_ERROR "Go compiler not found. Please install Go from https://go.dev/dl/")
endif()
set(GO_EXECUTABLE "${GO_EXECUTABLE}" CACHE FILEPATH "Go compiler" FORCE)
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
find_program(
  PYTHON_EXECUTABLE
  NAMES python3 python python3.exe python.exe
  HINTS "${ROBOT_LOCAL_PYTHON_ROOT}/bin"
  DOC "Python interpreter")
if(NOT PYTHON_EXECUTABLE)
  robot_install_python()
endif()
if(NOT PYTHON_EXECUTABLE)
  message(FATAL_ERROR "Python not found. Please install Python 3.x")
endif()
set(PYTHON_EXECUTABLE "${PYTHON_EXECUTABLE}" CACHE FILEPATH "Python interpreter" FORCE)
execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" --version
  OUTPUT_VARIABLE _PYTHON_VERSION_OUTPUT
  ERROR_VARIABLE _PYTHON_VERSION_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _PYTHON_VERSION_RESULT)
if(NOT _PYTHON_VERSION_OUTPUT)
  set(_PYTHON_VERSION_OUTPUT "${_PYTHON_VERSION_ERROR}")
endif()
if(_PYTHON_VERSION_RESULT EQUAL 0)
  message(STATUS "Found Python: ${PYTHON_EXECUTABLE} (${_PYTHON_VERSION_OUTPUT})")
else()
  message(FATAL_ERROR "Python found at ${PYTHON_EXECUTABLE} but failed to get version")
endif()

# --- Java / JDK ---
find_program(
  JAVA_EXECUTABLE
  NAMES java java.exe
  HINTS "${ROBOT_LOCAL_JAVA_HOME}/bin"
  DOC "Java runtime")
if(NOT JAVA_EXECUTABLE)
  robot_install_jdk()
endif()
if(NOT JAVA_EXECUTABLE)
  message(FATAL_ERROR "Java not found. Please install a JDK (https://adoptium.net/)")
endif()
set(JAVA_EXECUTABLE "${JAVA_EXECUTABLE}" CACHE FILEPATH "Java runtime" FORCE)
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

find_program(
  JAVAC_EXECUTABLE
  NAMES javac javac.exe
  HINTS "${ROBOT_LOCAL_JAVA_HOME}/bin"
  DOC "Java compiler (JDK)")
if(NOT JAVAC_EXECUTABLE)
  robot_install_jdk()
endif()
if(NOT JAVAC_EXECUTABLE)
  message(FATAL_ERROR "javac not found. A full JDK is required, not just JRE.")
endif()
set(JAVAC_EXECUTABLE "${JAVAC_EXECUTABLE}" CACHE FILEPATH "Java compiler (JDK)" FORCE)
execute_process(
  COMMAND "${JAVAC_EXECUTABLE}" -version
  OUTPUT_VARIABLE _JAVAC_VERSION_OUTPUT
  ERROR_VARIABLE _JAVAC_VERSION_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _JAVAC_VERSION_RESULT)
if(NOT _JAVAC_VERSION_OUTPUT)
  set(_JAVAC_VERSION_OUTPUT "${_JAVAC_VERSION_ERROR}")
endif()
if(_JAVAC_VERSION_RESULT EQUAL 0)
  message(STATUS "Found JDK compiler: ${JAVAC_EXECUTABLE} (${_JAVAC_VERSION_OUTPUT})")
else()
  message(FATAL_ERROR "javac found at ${JAVAC_EXECUTABLE} but failed to get version")
endif()

# --- Helm ---
find_program(
  HELM_EXECUTABLE
  NAMES helm helm.exe
  HINTS "${ROBOT_LOCAL_HELM_ROOT}/${ROBOT_HOST_OS}-${ROBOT_HELM_ARCH}" "${ROBOT_LOCAL_TOOL_BIN_DIR}"
  DOC "Helm package manager")
if(NOT HELM_EXECUTABLE)
  robot_install_helm()
endif()
if(NOT HELM_EXECUTABLE)
  message(FATAL_ERROR "Helm not found. Please install Helm from https://helm.sh/docs/intro/install/")
endif()
set(HELM_EXECUTABLE "${HELM_EXECUTABLE}" CACHE FILEPATH "Helm package manager" FORCE)
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
  HINTS "${ROBOT_LOCAL_TOOL_BIN_DIR}"
  DOC "go-task runner")
if(NOT TASK_EXECUTABLE)
  robot_install_task()
endif()
if(NOT TASK_EXECUTABLE)
  message(FATAL_ERROR "go-task not found. Please install from https://taskfile.dev/installation/")
endif()
set(TASK_EXECUTABLE "${TASK_EXECUTABLE}" CACHE FILEPATH "go-task runner" FORCE)
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

set(ROBOT_RUNTIME_PATH_DIRS)
foreach(_runtime_dir IN ITEMS
                            "${ROBOT_LOCAL_TOOL_BIN_DIR}"
                            "${ROBOT_LOCAL_GO_ROOT}/bin"
                            "${ROBOT_LOCAL_PYTHON_ROOT}/bin"
                            "${ROBOT_LOCAL_JAVA_HOME}/bin"
                            "${ROBOT_LOCAL_HELM_ROOT}/${ROBOT_HOST_OS}-${ROBOT_HELM_ARCH}")
  if(EXISTS "${_runtime_dir}")
    list(APPEND ROBOT_RUNTIME_PATH_DIRS "${_runtime_dir}")
  endif()
endforeach()
list(REMOVE_DUPLICATES ROBOT_RUNTIME_PATH_DIRS)
list(JOIN ROBOT_RUNTIME_PATH_DIRS "${ROBOT_PATH_SEPARATOR}" ROBOT_RUNTIME_PATH_PREFIX)
if(ROBOT_RUNTIME_PATH_PREFIX)
  set(ROBOT_RUNTIME_PATH "${ROBOT_RUNTIME_PATH_PREFIX}${ROBOT_PATH_SEPARATOR}$ENV{PATH}")
else()
  set(ROBOT_RUNTIME_PATH "$ENV{PATH}")
endif()

set(ROBOT_TASK_ENV "INSTALL_PATH=${ROBOT_INSTALL_PATH}" "PATH=${ROBOT_RUNTIME_PATH}" "GOBIN=${ROBOT_LOCAL_TOOL_BIN_DIR}")
if(EXISTS "${ROBOT_LOCAL_GO_ROOT}/bin/go")
  list(APPEND ROBOT_TASK_ENV "GOROOT=${ROBOT_LOCAL_GO_ROOT}")
endif()
if(EXISTS "${ROBOT_LOCAL_JAVA_HOME}/bin/java")
  list(APPEND ROBOT_TASK_ENV "JAVA_HOME=${ROBOT_LOCAL_JAVA_HOME}")
endif()

message(STATUS "Robot source dir: ${ROBOT_SOURCE_DIR}")
message(STATUS "Robot install path: ${ROBOT_INSTALL_PATH}")

# ============================================================================
# 1. Custom target: run task all
# ============================================================================

add_custom_target(
  robot ALL
  COMMAND "${CMAKE_COMMAND}" -E env ${ROBOT_TASK_ENV} "${TASK_EXECUTABLE}" all
  WORKING_DIRECTORY "${ROBOT_SOURCE_DIR}"
  COMMENT "Building robot: task all (INSTALL_PATH=${ROBOT_INSTALL_PATH})"
  USES_TERMINAL)

set_property(TARGET robot PROPERTY FOLDER "${PROJECT_NAME}/tools/robot")
